#include <assert.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <xmmintrin.h>
#include <string.h>
#include <math.h>

#define RAND01 (rand()%2)
double eps = 1e-10;

typedef struct
{
    double x;
    double y;
    double vx;
    double vy;   
    int colli_p;
    int colli_w;
    // Speculative pts
    /////////////////
    double x_n;
    double y_n;
    /////////////////
}  Particle;
typedef struct
{
    int pa;
    int pb;
    double time;
} Collision;
int compare (const void * a, const void * b)
{
    Collision *colli_A = (Collision*)a;
    Collision *colli_B = (Collision*)b;
    double cmpf = colli_A->time - colli_B->time;
    if(fabs(cmpf)<eps)
    {
        int cmpt = colli_A->pa - colli_B->pa;
        if(cmpt!=0)
            return cmpt;
        else
            return colli_A->pb - colli_B->pb;
    }
    else
        return cmpf<0?-1:1;
}
int N, L, r, S;
char mode[6];

double doubleRand(double min, double max) // return [min, max] double vars
{
    return min+(max-min)*(rand() / (double)RAND_MAX);
}
void bound_pos(Particle *p)
{
    int bnd_far = L-r;
    double tx=0,ty=0;
    if(p->x_n>bnd_far)
        tx = (p->x_n-bnd_far)/p->vx;
    else if(p->x_n<r)
        tx = (p->x_n-r)/p->vx;
    if(p->y_n>bnd_far)
        ty = (p->y_n-bnd_far)/p->vy;
    else if(p->y_n<r)
        ty = (p->y_n-r)/p->vy;
    
    tx =ty = tx>ty?tx:ty;
    p->x_n = p->x_n - tx*p->vx;
    p->y_n = p->y_n - ty*p->vy;
}
long long wall_clock_time()
{
#ifdef LINUX
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return (long long)(tp.tv_nsec + (long long)tp.tv_sec * 1000000000ll);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_usec * 1000 + (long long)tv.tv_sec * 1000000000ll);
#endif
}
int main()
{
    //srand(0);
    srand((unsigned)time(NULL));
    freopen("./inputs.txt","r",stdin);
    freopen("./outputs.txt","w",stdout);
    scanf("%d %d %d %d %s",&N, &L, &r, &S, mode);
    Particle *particles, *P_a, *P_b;
    particles = (Particle *)malloc(N * sizeof(Particle));
    Collision *colli;
    int i =0,j, t, idx, cnt, real_colli, wall_colli, output=0, bnd_far;
    // long long time1, time2;
    double x, y, vx, vy, lambda, lambda_1, lambda_2, time, r_sq_4;
    double dx1, dx2, dy1, dy2, Dx, Dy, DDpDD, dDpdD, dDmdD, Delta;
    bnd_far = L-r;
    if(!strcmp(mode,"print"))
        output = 1;
    while(scanf("%d %lf %lf %lf %lf", &idx,&x,&y,&vx,&vy)!=EOF)
    {
        i++;
        P_a = particles + idx;
        P_a->x = x;
        P_a->y = y;
        P_a->vx = vx;
        P_a->vy = vy;
        P_a->colli_p = 0;
        P_a->colli_w = 0;
    }
    if(i==0)
    {
        for(;i<N;i++)
        {
            P_a = particles + i;
            P_a->x = doubleRand(r,bnd_far);
            P_a->y = doubleRand(r,bnd_far);
            P_a->vx = (1 - 2*RAND01)*doubleRand(L/(double)8.0/r,L/(double)4.0);
            P_a->vy = (1 - 2*RAND01)*doubleRand(L/(double)8.0/r,L/(double)4.0);
            P_a->colli_p = 0;
            P_a->colli_w = 0;
        }
    }
    else if(i!=N)
    {
        fprintf(stderr, "Not enough particle parameters!\n");
        exit(1);
    }
    // Print initial position
    for(i=0; i<N; i++)
        printf("0 %d %10.8lf %10.8lf %10.8lf %10.8lf\n", i, 
                    particles[i].x, particles[i].y, particles[i].vx, particles[i].vy);
    // Naive Method
    /* Basic Idea:
    1. Build collision table (N+1)x(N+1), N is the wall. To check whether the collision happens.
    2. Build collision time table for processing.
    🎯
    */
    int *colli_mat = (int *)malloc(N*sizeof(int));
    memset(colli_mat,0, N*sizeof(int));
    Collision *colli_time = (Collision*)malloc(N*(N+1)/2*sizeof(Collision));
    int *colli_queue = (int *)malloc(N*sizeof(int));
    // Start sim
    // time1 = wall_clock_time();
    r_sq_4 = 4*r*r;
    for(t=0;t<S;t++)
    {
        // Step 1: speculate no collision happen, get new pos & v.
        for(i=0; i<N; i++)
        {
            particles[i].x_n = particles[i].x + particles[i].vx;
            particles[i].y_n = particles[i].y + particles[i].vy;
            // printf("[Debug:pos_n] %d %10.8f %10.8f\n",i, particles[i].x_n, particles[i].y_n);
        }
        // Step 2: find all possible collision independently. fill colli_mat and colli_time.
        cnt = 0;
        real_colli = 0;
        
        for(i=0; i<N; i++)
        {
            P_a = particles+i;
            //Case 1: collision with wall
            ///////////////
            lambda_1 = lambda_2 = 2;
            wall_colli = 0;
            // printf("[Debug:before_colli] %d %10.8f %10.8f\n",i,P_a->x_n,P_a->y_n);
            if(P_a->x_n<r)
            {
                lambda_1 = (r - P_a->x) / P_a->vx;
                wall_colli = 1;
            }
            else if(P_a->x_n>bnd_far)
            {
                lambda_1 = (bnd_far - P_a->x) / P_a->vx;
                wall_colli = 1;
            }

            if(P_a->y_n<r)
            {
                lambda_2 = (r - P_a->y) / P_a->vy;
                wall_colli = 1;
            }
            else if(P_a->y_n>bnd_far)
            {
                lambda_2 = (bnd_far - P_a->y) / P_a->vy;
                wall_colli = 1;
            }
            // printf("[Debug:lambda] %10.8f %10.8f\n",lambda_1, lambda_2);
            
            if(wall_colli)
            {
                // printf("[Debug:Colli_wall] %d %10.8f %10.8f\n",i,P_a->x_n,P_a->y_n);
                colli_time[cnt].pb = i;  //exchange pa,pb here to ensure that pa<pb
                lambda = lambda_1-lambda_2;
                if(lambda==0) // Cornor collision!
                {
                    colli_time[cnt].pa = -1; // -1 to present this case.
                    colli_time[cnt].time = lambda_1;
                }
                else if(lambda<0) // x wall collision!
                {
                    colli_time[cnt].pa = -2; // -2 to present this case.
                    colli_time[cnt].time = lambda_1;
                }
                else if(lambda>0) // y wall collision!
                {
                    colli_time[cnt].pa = -3; // -3 to present this case.
                    colli_time[cnt].time = lambda_2;
                }
                // if(colli_time[cnt].time < 10e-8) // && colli_time[cnt].time > -MinDec)
                //     colli_time[cnt].time = 0;
                cnt++;
            }
            ///////////////
            for(j=i+1; j<N; j++)
            {
                P_b = particles+j;
                dx1 = P_b->x - P_a->x;
                dy1 = P_b->y - P_a->y;
                // Early detection
                Dx = P_b->vx - P_a->vx;
                Dy = P_b->vy - P_a->vy;
                dDpdD = dx1*Dx + dy1*Dy;
                if(dDpdD>=0) // To judge the right direction
                    continue;
                // Case 2: overlap at startup:
                ////////////////
                Delta = dx1*dx1 + dy1*dy1;
                if(Delta - r_sq_4<=0 && Delta!=0)
                {
                    colli_time[cnt].time = 0.0;
                    colli_time[cnt].pa = i;
                    colli_time[cnt].pb = j; // pa always smaller than pb
                    cnt++;
                    continue; // no need to further detect.
                }
                ////////////////
                // Case 3: Normal collision case
                ////////////////
                DDpDD = Dx*Dx + Dy*Dy;
                dDmdD = dx1*Dy - dy1*Dx;
                Delta = r_sq_4*DDpDD - dDmdD*dDmdD;
                if(Delta<=0)
                    continue;
                Delta = sqrt(Delta);
                lambda = (-dDpdD - Delta)/DDpDD;
                // printf("[Debug:lambda]: %f\n", lambda);
                if(lambda<1)
                {
                    colli_time[cnt].time = lambda;
                    colli_time[cnt].pa = i;
                    colli_time[cnt].pb = j;
                    cnt++;
                }
                ////////////////
            }
        }
        // Step 3: sort collision table and process collision
        // Sort collision
        // printf("[Debug:num]: %d\n",cnt);
        qsort(colli_time, cnt, sizeof(Collision), compare);

        // Filter out true collision.
        for(i=0;i<cnt;i++)
        {
            colli = colli_time+i;
            /////
            // if(1 && (colli->pa == 2||colli->pb==2))
            // {
            //     printf("[Debug:inconsist] %d %d %10.8f\n",colli->pa, colli->pb, colli->time);
            // }
            /////
            if(colli->pa<0){ //wall collision
                if(!colli_mat[colli->pb])
                {
                    colli_mat[colli->pb] = 1;
                    colli_queue[real_colli++] = i;
                    particles[colli->pb].colli_w++;
                }
            }
            else if(!colli_mat[colli->pa])
            {
                if(!colli_mat[colli->pb])
                {
                    colli_mat[colli->pa] = 1;
                    colli_mat[colli->pb] = 1;
                    colli_queue[real_colli++] = i;
                    particles[colli->pa].colli_p++;
                    particles[colli->pb].colli_p++;
                }
            }
        }
        // Now let's see momentum; Potential improvements: separate diff case for diff thread
        for(i=0;i<real_colli;i++)
        {
            colli = colli_time + colli_queue[i];
            // printf("[Debug:neg] %d %d %10.8f\n",colli->pa, colli->pb, colli->time);
            if(colli->pa==-1) // Cornor colli;
            {
                P_a = particles + colli->pb;
                P_a->vx = -1*P_a->vx;
                P_a->vy = -1*P_a->vy;
                P_a->x_n = P_a->x+(1-2*colli->time)*P_a->vx;
                P_a->y_n = P_a->y+(1-2*colli->time)*P_a->vy; 
                bound_pos(P_a);
            }
            else if(colli->pa==-2)//  X wall colli;
            {
                P_a = particles + colli->pb;
                P_a->vx = -1*P_a->vx;
                P_a->x_n = P_a->x+(1-2*colli->time)*P_a->vx;
                bound_pos(P_a);
            }
            else if(colli->pa==-3)// Y wall colli;
            {
                P_a = particles + colli->pb;
                P_a->vy = -1*P_a->vy;
                P_a->y_n = P_a->y+(1-2*colli->time)*P_a->vy;
                // printf("[Debug:Y wall Colli] Pa: %10.8f %10.8f\n", P_a->x_n,P_a->y_n);
                bound_pos(P_a);
            }
            else // P-P colli;
            {
                P_a = particles + colli->pa;
                P_b = particles + colli->pb;
                // if two particles coincide at the exact same coordinates from the start of a time step, ignore it (no normal direction)
                // if(colli->time==0 && P_a->x==P_b->x && P_a->y==P_b->y)
                //     continue;
                P_a->x_n = P_a->x + colli->time*P_a->vx;
                P_a->y_n = P_a->y + colli->time*P_a->vy;
                P_b->x_n = P_b->x + colli->time*P_b->vx;
                P_b->y_n = P_b->y + colli->time*P_b->vy;
                Dx = P_b->x_n - P_a->x_n;
                Dy = P_b->y_n - P_a->y_n;
                Delta = 1 - colli->time;
                /* To reduce var: 
                 dx1: nv1; dy1: tv1; 
                 dx2: nv2; dy2: tv2;
                */
                dx1 = Dx*P_a->vx + Dy*P_a->vy;
                dy1 = Dx*P_a->vy - Dy*P_a->vx; 
                dx2 = Dx*P_b->vx + Dy*P_b->vy;
                dy2 = Dx*P_b->vy - Dy*P_b->vx; 
                DDpDD = Dx*Dx + Dy*Dy;
                if(DDpDD!=0)
                {
                    // Update velocities
                    P_a->vx = (dx2*Dx-dy1*Dy)/DDpDD;
                    P_a->vy = (dx2*Dy+dy1*Dx)/DDpDD;
                    P_b->vx = (dx1*Dx-dy2*Dy)/DDpDD;
                    P_b->vy = (dx1*Dy+dy2*Dx)/DDpDD;
                }
                // Update position
                P_a->x_n = P_a->x_n + Delta*P_a->vx;
                P_a->y_n = P_a->y_n + Delta*P_a->vy;
                bound_pos(P_a);
                P_b->x_n = P_b->x_n + Delta*P_b->vx;
                P_b->y_n = P_b->y_n + Delta*P_b->vy;
                bound_pos(P_b);
                //
            }
        }
        // Safe update
        for(i=0;i<N;i++)
        {
            P_a = particles+i;
            P_a->x = P_a->x_n;
            P_a->y = P_a->y_n;
            colli_mat[i] = 0;
        }
        // To Output Result:
        if(output)
        {
            for(i=0; i<N; i++)
                printf("%d %d %10.8lf %10.8lf %10.8lf %10.8lf\n",t+1, i, 
                    particles[i].x, particles[i].y, particles[i].vx, particles[i].vy);
        }
    }
    for(i=0; i<N; i++)
        printf("%d %d %10.8lf %10.8lf %10.8lf %10.8lf %d %d\n",S, i, 
                particles[i].x, particles[i].y, particles[i].vx, particles[i].vy,
                particles[i].colli_p, particles[i].colli_w);
                
    // time2=wall_clock_time();
    // time=((double)(time2 - time1)) / 1000000000;
    // printf("Time consumed: %10.8lf\n",time);
    
    fclose(stdin);
    fclose(stdout);
    free(particles);
    free(colli_time);
    free(colli_mat);
    free(colli_queue);
    return 0;
}