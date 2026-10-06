#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE   65536
#define MAX_FIELDS 64
#define MAX_TOP    10
#define MAX_GENRES 200
#define FIRST_YEAR 1800
#define LAST_YEAR  2025
#define YEAR_RANGE (LAST_YEAR - FIRST_YEAR + 1)

typedef struct {
    char title[256];
    int year;
    double revenue;
    double popularity;
    double score;
} TopMovie;

typedef struct {
    char name[64];
    double sum;
    long long cnt;
} GenreEntry;

void trim(char *s) {
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

void cleanGenre(char *s) {
    char temp[256];
    int j = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] != '"' && s[i] != '[' && s[i] != ']') temp[j++] = s[i];
    }
    temp[j] = '\0';
    strncpy(s, temp, 255);
    s[255] = '\0';
    trim(s);
}

int splitCSV(char *line, char **fields) {
    int count = 0, inQuotes = 0;
    fields[count++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '"') inQuotes = !inQuotes;
        else if (*p == ',' && !inQuotes) {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return count;
}

double toDouble(const char *s, int *ok) {
    char *end;
    double v = strtod(s, &end);
    *ok = (end != s);
    return (*ok ? v : 0.0);
}

int parseYear(const char *s) {
    for (int i = 0; s[i+3]; i++) {
        if (isdigit((unsigned char)s[i]) && isdigit((unsigned char)s[i+1]) &&
            isdigit((unsigned char)s[i+2]) && isdigit((unsigned char)s[i+3])) {
            int y = atoi(&s[i]);
            if (y >= FIRST_YEAR && y <= LAST_YEAR) return y;
        }
    }
    return 0;
}

void insertTop(TopMovie arr[], int *n, char *title, int year,
               double rev, double pop, double score) {
    if (score <= 0) return;
    if (*n < MAX_TOP) {
        int pos = (*n)++;
        while (pos > 0 && score > arr[pos - 1].score) {
            arr[pos] = arr[pos - 1];
            pos--;
        }
        strncpy(arr[pos].title, title, 255);
        arr[pos].title[255] = '\0';
        arr[pos].year = year;
        arr[pos].revenue = rev;
        arr[pos].popularity = pop;
        arr[pos].score = score;
        return;
    }
    if (score <= arr[*n - 1].score) return;
    int pos = *n - 1;
    while (pos > 0 && score > arr[pos - 1].score) {
        arr[pos] = arr[pos - 1];
        pos--;
    }
    strncpy(arr[pos].title, title, 255);
    arr[pos].title[255] = '\0';
    arr[pos].year = year;
    arr[pos].revenue = rev;
    arr[pos].popularity = pop;
    arr[pos].score = score;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    if (argc < 2) {
        if (rank == 0) fprintf(stderr, "Usage: %s <csv_file>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }
    const char *filename = argv[1];
MPI_Barrier(MPI_COMM_WORLD);
    double total_start = MPI_Wtime();
    double comp_time_local = 0.0, comm_time_local = 0.0;

    char *bigbuf = NULL;
    int *sendcounts = NULL, *displs = NULL;
    int numFields = 0;
    long long total_lines = 0;
    char header_line[MAX_LINE] = {0};
    int idxTitle = -1, idxRating = -1, idxRelease = -1, idxRev = -1, idxBudget = -1, idxPop = -1, idxGenre = -1;

    if (rank == 0) {
    printf("[Rank 0] Opening file: %s\n", filename);
    FILE *fp = fopen(filename, "r");
    if (!fp) { perror("File"); MPI_Abort(MPI_COMM_WORLD, 1); }

    char line[MAX_LINE]; char *fields[MAX_FIELDS];
    fgets(line, sizeof(line), fp);
    strncpy(header_line, line, MAX_LINE-1);
    numFields = splitCSV(line, fields);

    printf("[Rank 0] Header parsed, numFields=%d\n", numFields);

    for (int i = 0; i < numFields; i++) {
        trim(fields[i]);
        if (!strcasecmp(fields[i], "title")) idxTitle = i;
        else if (!strcasecmp(fields[i], "vote_average")) idxRating = i;
        else if (!strcasecmp(fields[i], "revenue")) idxRev = i;
        else if (!strcasecmp(fields[i], "budget")) idxBudget = i;
        else if (!strcasecmp(fields[i], "popularity")) idxPop = i;
        else if (!strcasecmp(fields[i], "release_date")) idxRelease = i;
        else if (!strcasecmp(fields[i], "genres")) idxGenre = i;
    }

        long long cap = 1024;
        char **lines = malloc(sizeof(char*) * cap);
        long long *llen = malloc(sizeof(long long) * cap);
        long long nlines = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (nlines >= cap) {
                cap *= 2;
                lines = realloc(lines, sizeof(char*) * cap);
                llen = realloc(llen, sizeof(long long) * cap);
            }
            int L = strlen(line);
            char *s = malloc(L+1);
            memcpy(s, line, L+1);
            lines[nlines] = s;
            llen[nlines] = L;
            nlines++;
        }
        fclose(fp);
        total_lines = nlines;

        sendcounts = malloc(sizeof(int) * nprocs);
        displs = malloc(sizeof(int) * nprocs);
        long long base = nlines / nprocs;
        int rem = nlines % nprocs;
        long long offset = 0;
    for (int r = 0; r < nprocs; r++) {
        long long start = r*base + (r<rem?r:rem);
        long long cnt = base + (r<rem?1:0);
        long long bytes = 0;
        for (long long i=start;i<start+cnt;i++) bytes += llen[i];
        sendcounts[r] = (int)bytes;
        displs[r] = (int)offset;
        printf("[Rank 0] Assigned chunk for Rank %d: start=%lld end=%lld (size=%lld)\n",
               r, start, start+cnt, bytes);
        offset += bytes;
    }

        long long total_bytes = offset;
        bigbuf = malloc(total_bytes);
        long long pos = 0;
        for(long long i=0;i<nlines;i++){
            memcpy(bigbuf+pos, lines[i], llen[i]);
            pos += llen[i];
            free(lines[i]);
        }

        free(lines); free(llen);
    }

    int meta[7] = { idxTitle, idxRating, idxRelease, idxRev, idxBudget, idxPop, idxGenre };
    MPI_Request reqs[10]; int reqcnt=0;
MPI_Barrier(MPI_COMM_WORLD);
    double comm_start = MPI_Wtime();
    MPI_Ibcast(meta, 7, MPI_INT, 0, MPI_COMM_WORLD, &reqs[reqcnt++]);
    MPI_Ibcast(&numFields, 1, MPI_INT, 0, MPI_COMM_WORLD, &reqs[reqcnt++]);
    MPI_Ibcast(&total_lines, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD, &reqs[reqcnt++]);
    MPI_Ibcast(header_line, MAX_LINE, MPI_CHAR, 0, MPI_COMM_WORLD, &reqs[reqcnt++]);
    if(rank!=0) sendcounts = malloc(sizeof(int)*nprocs);
    MPI_Ibcast(sendcounts, nprocs, MPI_INT, 0, MPI_COMM_WORLD, &reqs[reqcnt++]);
    MPI_Waitall(reqcnt, reqs, MPI_STATUSES_IGNORE);
    MPI_Barrier(MPI_COMM_WORLD);
    comm_time_local += MPI_Wtime() - comm_start;

    idxTitle = meta[0]; idxRating = meta[1]; idxRelease = meta[2];
    idxRev = meta[3]; idxBudget = meta[4]; idxPop = meta[5]; idxGenre = meta[6];

    int mybytes = sendcounts[rank];
    char *mybuf = malloc(mybytes);
    MPI_Barrier(MPI_COMM_WORLD);
    comm_start = MPI_Wtime();
    MPI_Request scatter_req;
    MPI_Iscatterv(bigbuf, sendcounts, displs, MPI_CHAR, mybuf, mybytes, MPI_CHAR, 0, MPI_COMM_WORLD, &scatter_req);
    MPI_Wait(&scatter_req, MPI_STATUS_IGNORE);
    printf("[Rank %d] Received totalLines = %lld, processing chunk of %d bytes\n",
       rank, total_lines, mybytes);
    MPI_Barrier(MPI_COMM_WORLD);
    comm_time_local += MPI_Wtime() - comm_start;

    if(rank==0 && bigbuf) free(bigbuf);
    free(displs);
MPI_Barrier(MPI_COMM_WORLD);
    double comp_start = MPI_Wtime();
    long long local_processed = 0;
    double sumYear_local[YEAR_RANGE]={0};
    long long countYear_local[YEAR_RANGE]={0};
    TopMovie topRev_local[MAX_TOP]={0}, topPop_local[MAX_TOP]={0};
    int nTopRev_local=0, nTopPop_local=0;
    char genreNames_local[MAX_GENRES][64]={{0}};
    double genreSum_local[MAX_GENRES]={0};
    long long genreCnt_local[MAX_GENRES]={0};
    int genreUsed_local=0;

    char *saveptr=NULL;
    char *p = mybuf;
    char *lineptr;
    while((lineptr=strtok_r(p,"\n",&saveptr))!=NULL){
        p=NULL;
        if(strlen(lineptr)<=1) continue;
        char *fields[MAX_FIELDS];
        int fc = splitCSV(lineptr, fields);
        if(fc<numFields) continue;
        local_processed++;

        char title[256]=""; double rating=0, rev=0, budget=0, pop=0; int year=0; int ok;
        if(idxTitle>=0){strncpy(title,fields[idxTitle],255);title[255]='\0';trim(title);}
        if(idxRating>=0) rating=toDouble(fields[idxRating], &ok);
        if(idxRev>=0) rev=toDouble(fields[idxRev], &ok);
        if(idxBudget>=0) budget=toDouble(fields[idxBudget], &ok);
        if(idxPop>=0) pop=toDouble(fields[idxPop], &ok);
        if(idxRelease>=0){char tmp[64]; strncpy(tmp, fields[idxRelease],63);tmp[63]='\0';trim(tmp);year=parseYear(tmp);}
        if(year>=FIRST_YEAR && year<=LAST_YEAR && rating>0){ sumYear_local[year-FIRST_YEAR]+=rating; countYear_local[year-FIRST_YEAR]++; }

        if(idxGenre>=0 && rating>0){
            char tmp[256]; strncpy(tmp, fields[idxGenre],255); tmp[255]='\0'; cleanGenre(tmp);
            char *first=strtok(tmp,",");
            if(first){ cleanGenre(first); trim(first); if(strlen(first)>0){ first[0]=toupper(first[0]); for(int i=1;first[i];i++) first[i]=tolower(first[i]);
                int found=-1; for(int g=0;g<genreUsed_local;g++){ if(!strcasecmp(genreNames_local[g],first)){found=g;break;}}
                if(found==-1 && genreUsed_local<MAX_GENRES){ strncpy(genreNames_local[genreUsed_local],first,63); genreNames_local[genreUsed_local][63]='\0'; found=genreUsed_local++; }
                if(found>=0){ genreSum_local[found]+=rating; genreCnt_local[found]++; }}}}

        insertTop(topRev_local,&nTopRev_local,title,year,rev,pop,rev);
        insertTop(topPop_local,&nTopPop_local,title,year,rev,pop,pop);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    comp_time_local = MPI_Wtime() - comp_start;
    free(mybuf);

    MPI_Request req_reduce[3];
MPI_Request gatherv_req;

    double *sumYear_global=NULL; long long *countYear_global=NULL; long long total_processed=0;
    if(rank==0){ sumYear_global=malloc(sizeof(double)*YEAR_RANGE); countYear_global=malloc(sizeof(long long)*YEAR_RANGE); }
MPI_Barrier(MPI_COMM_WORLD);
    comm_start = MPI_Wtime();
    MPI_Ireduce(sumYear_local,sumYear_global,YEAR_RANGE,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD,&req_reduce[0]);
    MPI_Ireduce(countYear_local,countYear_global,YEAR_RANGE,MPI_LONG_LONG,MPI_SUM,0,MPI_COMM_WORLD,&req_reduce[1]);
    MPI_Ireduce(&local_processed,&total_processed,1,MPI_LONG_LONG,MPI_SUM,0,MPI_COMM_WORLD,&req_reduce[2]);
    MPI_Waitall(3,req_reduce,MPI_STATUSES_IGNORE);
    MPI_Barrier(MPI_COMM_WORLD);
    comm_time_local += MPI_Wtime() - comm_start;

    MPI_Request req_gather[3];
    int topo_bytes = sizeof(TopMovie)*MAX_TOP;
    TopMovie *gTopRev=NULL, *gTopPop=NULL;
    if(rank==0){ gTopRev=malloc(topo_bytes*nprocs); gTopPop=malloc(topo_bytes*nprocs); }
MPI_Barrier(MPI_COMM_WORLD);
    comm_start = MPI_Wtime();
    MPI_Igather(topRev_local,topo_bytes,MPI_BYTE,gTopRev,topo_bytes,MPI_BYTE,0,MPI_COMM_WORLD,&req_gather[0]);
    MPI_Igather(topPop_local,topo_bytes,MPI_BYTE,gTopPop,topo_bytes,MPI_BYTE,0,MPI_COMM_WORLD,&req_gather[1]);

    GenreEntry *pack_local=NULL; int pack_bytes_local=genreUsed_local*sizeof(GenreEntry);
    if(pack_bytes_local>0){ pack_local=malloc(pack_bytes_local); for(int g=0;g<genreUsed_local;g++){ strncpy(pack_local[g].name,genreNames_local[g],63); pack_local[g].name[63]='\0'; pack_local[g].sum=genreSum_local[g]; pack_local[g].cnt=genreCnt_local[g]; } }
    int *recv_pack_bytes=NULL; if(rank==0) recv_pack_bytes=malloc(sizeof(int)*nprocs);
    MPI_Igather(&pack_bytes_local,1,MPI_INT,recv_pack_bytes,1,MPI_INT,0,MPI_COMM_WORLD,&req_gather[2]);
    MPI_Waitall(3,req_gather,MPI_STATUSES_IGNORE);
    MPI_Barrier(MPI_COMM_WORLD);
    comm_time_local += MPI_Wtime() - comm_start;

    int *genre_displs=NULL; GenreEntry *all_genres=NULL; int total_genre_bytes=0;
    if(rank==0){ genre_displs=malloc(sizeof(int)*nprocs); int off=0; for(int r=0;r<nprocs;r++){ genre_displs[r]=off; off+=recv_pack_bytes[r]; } total_genre_bytes=off; if(total_genre_bytes>0) all_genres=malloc(total_genre_bytes); }
MPI_Barrier(MPI_COMM_WORLD);
    comm_start = MPI_Wtime();
    MPI_Igatherv(pack_local,pack_bytes_local,MPI_BYTE,all_genres,recv_pack_bytes,genre_displs,MPI_BYTE,0,MPI_COMM_WORLD,&gatherv_req);
    MPI_Wait(&gatherv_req,MPI_STATUS_IGNORE);
    MPI_Barrier(MPI_COMM_WORLD);
    comm_time_local += MPI_Wtime() - comm_start;

    if(rank==0 && total_genre_bytes>0){
        GenreEntry merged[MAX_GENRES]={{0}};
        int merged_count=0;
        for(int i=0;i<total_genre_bytes/sizeof(GenreEntry);i++){
            GenreEntry *g=&all_genres[i];
            if(g->cnt==0) continue;
            int found=-1;
            for(int j=0;j<merged_count;j++){ if(!strcasecmp(merged[j].name,g->name)){found=j;break;} }
            if(found==-1){ merged[merged_count++]=*g; } else { merged[found].sum+=g->sum; merged[found].cnt+=g->cnt; }
        }
        TopMovie globalTopRev[MAX_TOP]={0}, globalTopPop[MAX_TOP]={0}; int nR=0,nP=0;
        for(int r=0;r<nprocs;r++){
            TopMovie *arrR=gTopRev+r*MAX_TOP; TopMovie *arrP=gTopPop+r*MAX_TOP;
            for(int t=0;t<MAX_TOP;t++){
                insertTop(globalTopRev,&nR,arrR[t].title,arrR[t].year,arrR[t].revenue,arrR[t].popularity,arrR[t].score);
                insertTop(globalTopPop,&nP,arrP[t].title,arrP[t].year,arrP[t].revenue,arrP[t].popularity,arrP[t].score);
            }
        }

        printf("\nTop %d Movies by Revenue:\n", MAX_TOP);
        for(int i=0;i<nR;i++) printf(" %2d. \"%s\" | Revenue: %.2f\n", i+1, globalTopRev[i].title, globalTopRev[i].revenue);

        printf("\nTop %d Movies by Popularity:\n", MAX_TOP);
        for(int i=0;i<nP;i++) printf(" %2d. \"%s\" | Year:%d | Popularity: %.2f\n", i+1, globalTopPop[i].title, globalTopPop[i].year, globalTopPop[i].popularity);

        printf("\nAverage Rating per Year:\n%-6s %-8s %-6s\n","Year","Count","Avg");
        for(int y=0;y<YEAR_RANGE;y++){
            if(countYear_global[y]>0)
                printf("%-6d %-8lld %-6.3f\n", FIRST_YEAR+y, countYear_global[y], sumYear_global[y]/countYear_global[y]);
        }

        printf("\nAverage Rating per Genre:\n%-20s %-8s %-6s\n","Genre","Count","Avg");
        for(int i=0;i<merged_count;i++){
            printf("%-20s %-8lld %-6.3f\n", merged[i].name, merged[i].cnt, merged[i].sum/merged[i].cnt);
        }

        free(gTopRev); free(gTopPop); free(sumYear_global); free(countYear_global);
        if(all_genres) free(all_genres);
        free(recv_pack_bytes); free(genre_displs);
    }
    if(pack_local) free(pack_local);

    MPI_Barrier(MPI_COMM_WORLD);
    double total_end = MPI_Wtime();
    double max_total=0, max_comp=0, max_comm=0;
    MPI_Reduce(&total_end,&max_total,1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);
    MPI_Reduce(&comp_time_local,&max_comp,1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);
    MPI_Reduce(&comm_time_local,&max_comm,1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);

    if(rank==0){
        printf("\nTotal Time: %.6f\n", max_total - total_start);
        printf("Computation Time: %.6f\n", max_comp);
        printf("Communication Time: %.6f\n", max_comm);
    }

    if(sendcounts) free(sendcounts);
    MPI_Finalize();
    return 0;
}
