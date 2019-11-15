#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <xmmintrin.h>
#include <string.h>
#include <math.h>
#include "timer.h"
#include <mpi.h>

#define RAND01 (rand()%2)
#define eps 1e-10

typedef enum {
    MODE_PRINT,
    MODE_PERF
} simulation_mode_t;

typedef struct
{
    double x;
    double y;
    double vx;
    double vy;   
    int colli_p;
    int colli_w;
}  Particle;

typedef struct
{
    int pa;
    int pb;
    double time;
} Collision;

int n, l, r, s, bnd_far, r_sq_4, num_cmp, count;
int num_slave, myid, nprocs, slave_id, offset, send_size,
    chunk_size, last_chunk_size, dst_id;
Particle *particles, *P_a, *P_b;
Collision *colli_time, *colli;

MPI_Datatype Particle_Type, Colli_Type;

#define MASTER_ID 0

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

double doubleRand(double min, double max) // return [min, max] double vars
{
    return min+(max-min)*(rand() / (double)RAND_MAX);
}

void randomise_particles()
{
    /* Implement randomisation */
    int i;
    for(i=0; i<n; i++)
    {
        particles[i].x = doubleRand(r,bnd_far);
        particles[i].y = doubleRand(r,bnd_far);
        particles[i].vx = (1 - 2*RAND01)*doubleRand(l/(double)8.0/r,l/(double)4.0);
        particles[i].vy = (1 - 2*RAND01)*doubleRand(l/(double)8.0/r,l/(double)4.0);
    }
}

void bound_pos(Particle *p)
{
    double tx=0,ty=0;
    if(p->x>bnd_far)
        tx = (p->x-bnd_far)/p->vx;
    else if(p->x<r)
        tx = (p->x-r)/p->vx;
    if(p->y>bnd_far)
        ty = (p->y-bnd_far)/p->vy;
    else if(p->y<r)
        ty = (p->y-r)/p->vy;

    tx =ty = tx>ty?tx:ty;
    p->x = p->x - tx*p->vx;
    p->y = p->y - ty*p->vy;
}

void check_wall_colli(int chunk_idx, int num_item)
{
    double lambda_1, lambda_2, lambda;
    int i, wall_colli;
    particle_t *P_a;
    double x_n, y_n;
    int offset = chunk_idx * chunk_size;
    for(i=0; i<num_item; i++)
    {
        P_a = particles + i + offset;
        x_n = P_a->x + P_a->vx;
        y_n = P_a->y + P_a->vy;
        //Case 1: collision with wall
        ///////////////
        lambda_1 = lambda_2 = 2;
        wall_colli = 0;
        if(x_n<r)
        {
            lambda_1 = (r - P_a->x) / P_a->vx;
            wall_colli = 1;
        }
        else if(x_n>bnd_far)
        {
            lambda_1 = (bnd_far - P_a->x) / P_a->vx;
            wall_colli = 1;
        }

        if(y_n<r)
        {
            lambda_2 = (r - P_a->y) / P_a->vy;
            wall_colli = 1;
        }
        else if(y_n>bnd_far)
        {
            lambda_2 = (bnd_far - P_a->y) / P_a->vy;
            wall_colli = 1;
        }
        if(wall_colli)
        {
            count++; 
            colli_time[count].pb = i;
            lambda = lambda_1-lambda_2;
            if(fabs(lambda)<eps) // Cornor collision!
            {
                colli_time[count].pa = -1; // -1 to present this case.
                colli_time[count].time = lambda_1;
            }
            else if(lambda<0) // x wall collision!
            {
                colli_time[count].pa = -2; // -2 to present this case.
                colli_time[count].time = lambda_1;
            }
            else if(lambda>0) // y wall collision!
            {
                colli_time[count].pa = -3; // -3 to present this case.
                colli_time[count].time = lambda_2;
            }
        }
        ///////////////
    }
}

void check_pp_colli(chunk_idx_A, chunk_idx_B, num_item_A, num_item_B)
{
    double dx1, dy1, Delta, Dx, Dy, dDpdD, dDmdD, DDpDD, lambda;
    int offset_A, offset_B, i, j;
    offset_A = chunk_idx_A * chunk_size;
    offset_B = chunk_idx_B * chunk_size;
    for(i=0; i<num_item_A; i++)
    {
        if(chunk_idx_A == chunk_idx_B)
            j = i + 1;
        else
            j = 0;
        for(; j<num_item_B; j++)
        {
            P_a = particles + offset_A + i;
            P_b = particles + offset_B + j;
            dx1 = P_b->x - P_a->x;
            dy1 = P_b->y - P_a->y;
            // early stop
            Dx = P_b->vx - P_a->vx;
            Dy = P_b->vy - P_a->vy;
            dDpdD = dx1*Dx + dy1*Dy;
            if(dDpdD<0) // To ensure the right direction
            {
                // Case 2: overlap at startup:
                ////////////////
                Delta = dx1*dx1 + dy1*dy1;
                if(Delta - r_sq_4<=0 && Delta!=0)
                {
                    count++;
                    colli_time[count].time = 0.0;
                    colli_time[count].pa = pa_idx[i];
                    colli_time[count].pb = pb_idx[i];
                }
                ////////////////
                else
                {
                    // Case 3: Normal collision case
                    ////////////////
                    DDpDD = Dx*Dx + Dy*Dy;
                    dDmdD = dx1*Dy - dy1*Dx;
                    Delta = r_sq_4*DDpDD - dDmdD*dDmdD;
                    if(Delta>0)
                    {
                        Delta = sqrt(Delta);
                        lambda = (-dDpdD - Delta)/DDpDD;
                        // printf("[Debug:lambda]: %f\n", lambda);
                        if(lambda<1)
                        {
                            count++;
                            colli_time[count].time = lambda;
                            colli_time[count].pa = pa_idx[i];
                            colli_time[count].pb = pb_idx[i];
                        }
                    }
                    ////////////////
                }
            }
        }
    }
}

/*
Data distribution policy: eg. when num_slave = 5
v0. 
Job_mat:                 Send_mat:
   [[1, 1, 5, 3, 5], |      [[1, 1, 0, 1, 0],
    [0, 2, 2, 1, 4], |       [0, 1, 1, 0, 1],
    [0, 0, 3, 3, 2], |       [1, 0, 1, 1, 0],
    [0, 0, 0, 4, 4], |       [0, 1, 0, 1, 1],
    [0, 0, 0, 0, 5]] |       [1, 0, 1, 0, 1]]
v1.
Job_mat:                 Send_mat:
   [[1, 1, 1, 5, 5], |      [[1, 1, 1, 0, 0],
    [0, 2, 2, 2, 4], |       [0, 1, 1, 1, 0],
    [0, 0, 3, 3, 3], |       [0, 0, 1, 1, 1],
    [0, 0, 0, 4, 4], |       [0, 1, 0, 1, 1],
    [0, 0, 0, 0, 5]] |       [1, 0, 0, 1, 1]]
*/
int gen_comm_mat(int (*send_mat)[nprocs], int* pa_idx, int* pb_idx)
{
    int i, j, k, proc, cnt=0, job_cnt=0;
    int flag_mat[num_slave][num_slave];
    memset(flag_mat[0] ,0, num_slave*num_slave*sizeof(int));
    for(k=num_slave; k>0; k--)
    {
        j = num_slave - k;
        for(i=0; i<k; i++)
        {
            proc = cnt % num_slave;
            if(!flag_mat[proc][i])
            {
                flag_mat[proc][i] = 1;
                send_mat[proc][send_mat[proc][num_slave]] = i;
                send_mat[proc][num_slave]++;
            }
            if(!flag_mat[proc][j])
            {
                flag_mat[proc][j] = 1;
                send_mat[proc][send_mat[proc][num_slave]] = j;
                send_mat[proc][num_slave]++;
            }
            if(proc+1==myid)
            {
                pa_idx[job_cnt] = i;
                pb_idx[job_cnt] = j;
                job_cnt ++;
            }
            cnt++;
            j++;
        }
    }
    return job_cnt;
}

void gen_parti_type(MPI_Datatype *Type_ptr)
{
    MPI_Datatype type[6] = { 
        MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE, 
        MPI_INT, MPI_INT };
    int blocklen[6] = { 1, 1, 1, 1, 1, 1 };
    MPI_Aint disp[6];

    disp[0] = offsetof(Particle, x);
    disp[1] = offsetof(Particle, y);
    disp[2] = offsetof(Particle, vx);
    disp[3] = offsetof(Particle, vy);
    disp[4] = offsetof(Particle, colli_p);
    disp[5] = offsetof(Particle, colli_w);
    MPI_Type_create_struct(6, blocklen, disp, type, Type_ptr);
    MPI_Type_commit(Type_ptr);
}

void gen_colli_type(MPI_Datatype *Type_ptr)
{
    MPI_Datatype type[3] = { MPI_INT, MPI_INT, MPI_DOUBLE };
    int blocklen[3] = { 1, 1, 1 };
    MPI_Aint disp[3];

    disp[0] = offsetof(Collision, pa);
    disp[1] = offsetof(Collision, pb);
    disp[2] = offsetof(Collision, time);
    MPI_Type_create_struct(3, blocklen, disp, type, Type_ptr);
    MPI_Type_commit(Type_ptr);
}

void print_particles(int step)
{
    int i;
    for (i = 0; i < n; i++) {
        printf("%d %d %10.8lf %10.8lf %10.8lf %10.8lf\n", step, i, particles[i].x, particles[i].y,
            particles[i].vx, particles[i].vy);
    }
}

void print_statistics(int num_step)
{
    int i;
    for (i = 0; i < n; i++) {
        printf("%d %d %10.8lf %10.8lf %10.8lf %10.8lf %d %d\n", num_step, i, particles[i].x,
            particles[i].y, particles[i].vx, particles[i].vy,
            particles[i].colli_p, particles[i].colli_w);
    }
}
/*
Master's job: 
    * init resource; 
    * scatter data;
    * gather res;
*/
void master()
{
    StartTimer();
    // srand((unsigned)time(NULL));
    srand(0);
    char mode_buf[6];
    scanf("%d", &n);
    scanf("%d", &l);
    scanf("%d", &r);
    scanf("%d", &s);
    scanf("%5s", mode_buf);
    
    // Use collective func.
    MPI_Bcast(&n, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(&l, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(&r, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(&s, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    ///////////
    int i, j, k, step;
    double x, y, vx, vy;
    simulation_mode_t mode;
    int send_mat[num_slave][nprocs];
    memset(send_mat[0] ,0, num_slave*nprocs*sizeof(int));
    gen_comm_mat(send_mat, NULL, NULL);

    bnd_far = l - r;
    num_cmp = n * (n+1) / 2;
    chunk_size = (n-1) / num_slave + 1;
    last_chunk_size = n % chunk_size;
    last_chunk_size = last_chunk_size?last_chunk_size:chunk_size;
    particles = (Particle *)malloc(n * sizeof(Particle));
    j = 0;
    while (scanf("%d %lf %lf %lf %lf", &i, &x, &y, &vx, &vy) != EOF) {
        j++;
        particles[i].x = x;
        particles[i].y = y;
        particles[i].vx = vx;
        particles[i].vy = vy;
        particles[i].colli_p = 0;
        particles[i].colli_w = 0;
    }
    if (j==0) {
        randomise_particles();
    }
    else if(j!=n){
        fprintf(stderr, "Not enough particle parameters!\n");
        exit(1);
    }
    mode = strcmp(mode_buf, "print") == 0 ? MODE_PRINT : MODE_PERF;
    
    colli_time = (Collision *)malloc(num_cmp *sizeof(Collision));
    print_particles(0);
    /*
        start sim
    */
    for(step = 0; step < 1; step++)
    {
        // Scatter particles
        for(k=0;k<num_slave;k++)
        {
            for(i=0;i<send_mat[k][num_slave];i++)
            {
                offset = send_mat[k][i] * chunk_size;
                send_size = send_mat[k][i]==num_slave-1?last_chunk_size:chunk_size;
                MPI_Send(particles+offset, send_size, Particle_Type, k+1, i, MPI_COMM_WORLD);
            }
        }
    }
}

void slave()
{
    MPI_Bcast(&n, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(&l, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(&r, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(&s, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);

    int i, j, k, step, num_chunk, major_chunk, num_chunk_cmp;
    num_chunk_cmp = num_slave * (num_slave + 1) / 2;
    num_chunk_cmp = (num_chunk_cmp-1) / num_slave +1;
    int chunk_map[num_slave], chunk_size_list[num_chunk_cmp],
        pa_idx[num_chunk_cmp], pb_idx[num_chunk_cmp];
    int send_mat[num_slave][nprocs];
    MPI_Status Stat;
    memset(send_mat[0], 0, num_slave*nprocs*sizeof(int));
    memset(chunk_map, -1, num_slave*sizeof(int));
    num_chunk_cmp = gen_comm_mat(send_mat, pa_idx, pb_idx);

    bnd_far = l - r;
    r_sq_4 = r * r * 4;
    chunk_size = (n-1) / num_slave + 1;
    num_chunk = send_mat[slave_id][num_slave];
    last_chunk_size = n % chunk_size;
    last_chunk_size = last_chunk_size?last_chunk_size:chunk_size;
    n = (num_chunk - 1) * chunk_size + last_chunk_size;
    num_cmp = n * (n+1) / 2;
    particles = (Particle *)malloc(num_chunk * chunk_size * sizeof(Particle));
    colli_time = (Collision *)malloc(num_cmp *sizeof(Collision));

    /*
        start sim
    */
    for(step = 0; step < 1; step++)
    {
        // Scatter particles
        for(i=0;i<num_chunk;i++)
        {
            offset = chunk_size * i;
            send_size = send_mat[slave_id][i]==num_slave-1?last_chunk_size:chunk_size;
            MPI_Recv(particles+offset, send_size, Particle_Type, MASTER_ID, i, MPI_COMM_WORLD, &Stat);
            if(send_mat[slave_id][i]==slave_id)
            {
                major_chunk = i;
            }
            chunk_map[send_mat[slave_id][i]] = i;
            chunk_size_list[i] = send_size;
        }
        // 
        count = 0;
        check_wall_colli(major_chunk, chunk_size_list[major_chunk]);
        for(k=0; k<num_chunk_cmp; k++)
        {
            i = pa_idx[k];
            j = pb_idx[k];
            check_pp_colli(chunk_map[i], chunk_map[j], chunk_size_list[i], chunk_size_list[j]);
        }
    }


}



int main(int argc, char ** argv)
{
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);

    if (nprocs < 2)
    {
        fprintf(stderr, "#Proc >= 2 !\n");
    }
    num_slave = nprocs - 1;
    slave_id = myid - 1;
    gen_parti_type(&Particle_Type);
    gen_colli_type(&Colli_Type);
    /*
    Master-Slave Pattern
    */
    if (myid == MASTER_ID)
    {
        master();
    }
    else
    {
        slave();
    }
    MPI_Finalize();
    return 0;
}