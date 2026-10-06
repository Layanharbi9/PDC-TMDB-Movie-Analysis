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
        if (s[i] != '"' && s[i] != '[' && s[i] != ']')
            temp[j++] = s[i];
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

    double total_start = 0.0;
    double comm_time_local = 0.0, comp_time_local = 0.0;
    double comm_time = 0.0, comp_time = 0.0;

    MPI_Barrier(MPI_COMM_WORLD);
    total_start = MPI_Wtime();

    int idxTitle=-1, idxRating=-1, idxRelease=-1, idxRev=-1;
    int idxBudget=-1, idxPop=-1, idxGenre=-1;
    int numFields = 0;
    long long total_lines = 0;

    char *bigbuf = NULL;
    int *sendcounts = NULL;
    int *displs = NULL;
    int header_len = 0;
    char header_line[MAX_LINE] = {0};

    if (rank == 0) {
        printf("[Rank 0] Opening file: %s\n", filename);
        FILE *fp = fopen(filename, "r");
        if (!fp) { perror("File"); MPI_Abort(MPI_COMM_WORLD,1); }

        char line[MAX_LINE];
        char *fields[MAX_FIELDS];

        if (!fgets(line, sizeof(line), fp)) {
            fprintf(stderr,"Empty file\n");
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD,1);
        }
        strncpy(header_line, line, MAX_LINE-1);
        header_line[MAX_LINE-1] = '\0';
        header_len = (int)strlen(header_line);

        numFields = splitCSV(line, fields);
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
        char **lines = (char**) malloc(sizeof(char*) * cap);
        long long *llen = (long long*) malloc(sizeof(long long) * cap);
        long long nlines = 0;

        while (fgets(line, sizeof(line), fp)) {
            int L = (int)strlen(line);
            char *s = (char*) malloc(L + 1);
            memcpy(s, line, L+1);
            if (nlines >= cap) {
                cap *= 2;
                lines = (char**) realloc(lines, sizeof(char*) * cap);
                llen  = (long long*) realloc(llen, sizeof(long long) * cap);
            }
            lines[nlines] = s;
            llen[nlines] = L;
            nlines++;
        }
        fclose(fp);

        total_lines = nlines;

        printf("[Rank 0] Total lines in file: %lld\n", total_lines);
        printf("[Rank 0] File read into memory.\n\n");

        sendcounts = (int*) malloc(sizeof(int) * nprocs);
        displs = (int*) malloc(sizeof(int) * nprocs);

        long long base = nlines / nprocs;
        int rem = (int)(nlines % nprocs);

        long long offset = 0;
        for (int r = 0; r < nprocs; ++r) {
            long long start = r * base + (r < rem ? r : rem);
            long long cnt = base + (r < rem ? 1 : 0);
            long long bytes = 0;
            for (long long i = start; i < start + cnt; ++i) bytes += llen[i];
            sendcounts[r] = (int) bytes;
            displs[r] = (int) offset;
            offset += bytes;
        }

        long long total_bytes = offset;
        bigbuf = (char*) malloc(total_bytes + 1);
        long long pos = 0;
        for (long long i = 0; i < nlines; ++i) {
            memcpy(bigbuf + pos, lines[i], (size_t)llen[i]);
            pos += llen[i];
            free(lines[i]);
        }
        bigbuf[total_bytes] = '\0';
        free(lines);
        free(llen);
    }

    int meta[7];
    if (rank == 0) {
        meta[0]=idxTitle; meta[1]=idxRating; meta[2]=idxRelease;
        meta[3]=idxRev; meta[4]=idxBudget; meta[5]=idxPop; meta[6]=idxGenre;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t_comm_start = MPI_Wtime();

    MPI_Bcast(meta, 7, MPI_INT, 0, MPI_COMM_WORLD);
    idxTitle = meta[0]; idxRating = meta[1]; idxRelease = meta[2];
    idxRev = meta[3]; idxBudget = meta[4]; idxPop = meta[5]; idxGenre = meta[6];

    MPI_Bcast(&numFields, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&total_lines, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&header_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) header_line[0] = '\0';
    MPI_Bcast(header_line, header_len + 1, MPI_CHAR, 0, MPI_COMM_WORLD);

    int *recvcounts = (int*) malloc(sizeof(int) * nprocs);
    if (rank == 0) {
        for (int r = 0; r < nprocs; ++r) recvcounts[r] = sendcounts[r];
    }
    MPI_Bcast(recvcounts, nprocs, MPI_INT, 0, MPI_COMM_WORLD);

    int mybytes = recvcounts[rank];
    char *mybuf = (char*) malloc((size_t)mybytes + 1);

    MPI_Scatterv(bigbuf, sendcounts, displs, MPI_CHAR,
                 mybuf, mybytes, MPI_CHAR,
                 0, MPI_COMM_WORLD);
    mybuf[mybytes] = '\0';


    MPI_Barrier(MPI_COMM_WORLD);
    double t_comm_end = MPI_Wtime();
    comm_time_local += (t_comm_end - t_comm_start);

    long long base = total_lines / nprocs;
    int rem = (int)(total_lines % nprocs);
    long long start_line = rank * base + (rank < rem ? rank : rem);
    long long count_lines = base + (rank < rem ? 1 : 0);
    long long end_line = start_line + count_lines;

    for (int r = 0; r < nprocs; ++r) {
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == r) {
            printf("[Rank %d] Assigned chunk: start=%lld end=%lld (size=%lld)\n",
                   rank, start_line, end_line, count_lines);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    double sumYear_local[YEAR_RANGE];
    long long countYear_local[YEAR_RANGE];
    for (int i = 0; i < YEAR_RANGE; ++i) { sumYear_local[i]=0.0; countYear_local[i]=0; }

    char genreNames_local[MAX_GENRES][64];
    double genreSum_local[MAX_GENRES];
    long long genreCnt_local[MAX_GENRES];
    int genreUsed_local = 0;
    for (int g = 0; g < MAX_GENRES; ++g) { genreSum_local[g]=0.0; genreCnt_local[g]=0; genreNames_local[g][0]='\0'; }

    TopMovie topRev_local[MAX_TOP];
    TopMovie topPop_local[MAX_TOP];
    int nTopRev_local = 0, nTopPop_local = 0;
    for (int t = 0; t < MAX_TOP; ++t) { topRev_local[t].title[0]='\0'; topRev_local[t].score=0.0; topPop_local[t].title[0]='\0'; topPop_local[t].score=0.0; }

    for (int r = 0; r < nprocs; ++r) {
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == r) {
            printf("[Rank %d] Starting local computation on %lld bytes (~%lld lines)\n",
                   rank, (long long)mybytes, count_lines);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double comp_start = MPI_Wtime();

    char *saveptr = NULL;
    char *p = mybuf;
    char *lineptr;
    long long local_processed = 0;

    while ((lineptr = strtok_r(p, "\n", &saveptr)) != NULL) {
        p = NULL;
        if (strlen(lineptr) <= 1) continue;

        char *fields[MAX_FIELDS];
        int fc = splitCSV(lineptr, fields);
        if (fc < numFields) continue;

        local_processed++;

        char title[256] = "";
        double rating = 0, rev = 0, budget = 0, pop = 0;
        int year = 0;
        int ok;

        if (idxTitle>=0) { strncpy(title, fields[idxTitle],255); title[255]='\0'; trim(title); }
        if (idxRating>=0) rating = toDouble(fields[idxRating], &ok);
        if (idxRev>=0)    rev = toDouble(fields[idxRev], &ok);
        if (idxBudget>=0) budget = toDouble(fields[idxBudget], &ok);
        if (idxPop>=0)    pop = toDouble(fields[idxPop], &ok);

        if (idxRelease>=0) {
            char tmp[64];
            strncpy(tmp, fields[idxRelease],63);
            tmp[63]='\0';
            trim(tmp);
            year = parseYear(tmp);
        }

        if (year >= FIRST_YEAR && year <= LAST_YEAR && rating > 0) {
            sumYear_local[year-FIRST_YEAR] += rating;
            countYear_local[year-FIRST_YEAR]++;
        }

        if (idxGenre>=0 && rating>0) {
            char tmp[256];
            strncpy(tmp, fields[idxGenre],255);
            tmp[255]='\0';
            cleanGenre(tmp);

            char *first = strtok(tmp, ",");
            if (first) {
                cleanGenre(first);
                trim(first);
                if (strlen(first) > 0) {
                    first[0] = toupper((unsigned char)first[0]);
                    for (int i = 1; first[i]; i++) first[i] = tolower((unsigned char)first[i]);

                    int found = -1;
                    for (int g=0; g<genreUsed_local; g++) {
                        if (!strcasecmp(genreNames_local[g], first)) { found = g; break; }
                    }
                    if (found == -1 && genreUsed_local < MAX_GENRES) {
                        strncpy(genreNames_local[genreUsed_local], first,63);
                        genreNames_local[genreUsed_local][63] = '\0';
                        found = genreUsed_local++;
                    }
                    if (found >= 0) {
                        genreSum_local[found] += rating;
                        genreCnt_local[found]++;
                    }
                }
            }
        }

        insertTop(topRev_local, &nTopRev_local, title, year, rev, pop, rev);
        insertTop(topPop_local, &nTopPop_local, title, year, rev, pop, pop);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double comp_end = MPI_Wtime();
    comp_time_local = comp_end - comp_start;

    for (int r = 0; r < nprocs; ++r) {
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == r) {
            printf("[Rank %d] Finished local computation: processed %lld valid lines. Preparing to send partial results to Rank 0.\n",
                   rank, local_processed);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    free(mybuf);
    if (rank == 0 && bigbuf) { free(bigbuf); free(sendcounts); free(displs); }

    double *sumYear_global = NULL;
    long long *countYear_global = NULL;
    if (rank == 0) {
        sumYear_global = (double*) malloc(sizeof(double) * YEAR_RANGE);
        countYear_global = (long long*) malloc(sizeof(long long) * YEAR_RANGE);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    t_comm_start = MPI_Wtime();

    MPI_Reduce(sumYear_local, sumYear_global, YEAR_RANGE, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(countYear_local, countYear_global, YEAR_RANGE, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    long long local_ll = local_processed;
    long long total_processed = 0;
    MPI_Reduce(&local_ll, &total_processed, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    int topo_chunk_bytes = sizeof(TopMovie) * MAX_TOP;
    TopMovie *gather_topRev = NULL;
    TopMovie *gather_topPop = NULL;
    if (rank == 0) {
        gather_topRev = (TopMovie*) malloc((size_t)topo_chunk_bytes * nprocs);
        gather_topPop = (TopMovie*) malloc((size_t)topo_chunk_bytes * nprocs);
    }
    MPI_Gather(topRev_local, topo_chunk_bytes, MPI_BYTE,
               gather_topRev, topo_chunk_bytes, MPI_BYTE,
               0, MPI_COMM_WORLD);
    MPI_Gather(topPop_local, topo_chunk_bytes, MPI_BYTE,
               gather_topPop, topo_chunk_bytes, MPI_BYTE,
               0, MPI_COMM_WORLD);

    GenreEntry *pack_local = NULL;
    int pack_bytes_local = 0;
    if (genreUsed_local > 0) {
        pack_bytes_local = genreUsed_local * (int)sizeof(GenreEntry);
        pack_local = (GenreEntry*) malloc((size_t)pack_bytes_local);
        for (int g = 0; g < genreUsed_local; ++g) {
            strncpy(pack_local[g].name, genreNames_local[g], 63);
            pack_local[g].name[63] = '\0';
            pack_local[g].sum = genreSum_local[g];
            pack_local[g].cnt = genreCnt_local[g];
        }
    }

    int *recv_pack_bytes = NULL;
    if (rank == 0) recv_pack_bytes = (int*) malloc(sizeof(int) * nprocs);
    MPI_Gather(&pack_bytes_local, 1, MPI_INT, recv_pack_bytes, 1, MPI_INT, 0, MPI_COMM_WORLD);

    GenreEntry *all_genres_packed = NULL;
    int *genre_displs = NULL;
    int total_genre_bytes = 0;
    if (rank == 0) {
        genre_displs = (int*) malloc(sizeof(int) * nprocs);
        int off = 0;
        for (int r = 0; r < nprocs; ++r) {
            genre_displs[r] = off;
            off += recv_pack_bytes[r];
        }
        total_genre_bytes = off;
        if (total_genre_bytes > 0) all_genres_packed = (GenreEntry*) malloc((size_t)total_genre_bytes);
    }

    MPI_Gatherv(pack_local, pack_bytes_local, MPI_BYTE,
                all_genres_packed, recv_pack_bytes, genre_displs, MPI_BYTE,
                0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    t_comm_end = MPI_Wtime();
    comm_time_local += (t_comm_end - t_comm_start);

    if (pack_local) free(pack_local);

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        printf("[Rank 0] Gathering partial results using MPI_Gather / MPI_Gatherv.\n");
        printf("[Rank 0] Combining results and computing final statistics...\n\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Reduce(&comm_time_local, &comm_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&comp_time_local, &comp_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double total_end = MPI_Wtime();

    if (rank == 0) {
        TopMovie globalTopRev[MAX_TOP];
        TopMovie globalTopPop[MAX_TOP];
        int nGlobalRev = 0, nGlobalPop = 0;
        for (int r = 0; r < nprocs; ++r) {
            TopMovie *chunk = gather_topRev + r * MAX_TOP;
            for (int k = 0; k < MAX_TOP; ++k) {
                if (chunk[k].score > 0.0)
                    insertTop(globalTopRev, &nGlobalRev, chunk[k].title, chunk[k].year, chunk[k].revenue, chunk[k].popularity, chunk[k].score);
            }
        }
        for (int r = 0; r < nprocs; ++r) {
            TopMovie *chunk = gather_topPop + r * MAX_TOP;
            for (int k = 0; k < MAX_TOP; ++k) {
                if (chunk[k].score > 0.0)
                    insertTop(globalTopPop, &nGlobalPop, chunk[k].title, chunk[k].year, chunk[k].revenue, chunk[k].popularity, chunk[k].score);
            }
        }

        char genreNames_global[MAX_GENRES][64];
        double genreSum_global[MAX_GENRES];
        long long genreCnt_global[MAX_GENRES];
        int genreUsed_global = 0;
        for (int i = 0; i < MAX_GENRES; ++i) { genreSum_global[i]=0.0; genreCnt_global[i]=0; genreNames_global[i][0]='\0'; }

        int cursor = 0;
        while (cursor < total_genre_bytes) {
            GenreEntry *e = (GenreEntry*) ((char*)all_genres_packed + cursor);
            int found = -1;
            for (int g = 0; g < genreUsed_global; ++g) {
                if (!strcasecmp(genreNames_global[g], e->name)) { found = g; break; }
            }
            if (found == -1 && genreUsed_global < MAX_GENRES) {
                strncpy(genreNames_global[genreUsed_global], e->name, 63);
                genreNames_global[genreUsed_global][63] = '\0';
                found = genreUsed_global++;
            }
            if (found >= 0) {
                genreSum_global[found] += e->sum;
                genreCnt_global[found] += e->cnt;
            }
            cursor += sizeof(GenreEntry);
        }

        printf("=====================================\n");
        printf("MPI Distributed (blocking version) – TMDB Movie Analysis\n");
        printf("Movies processed: %lld\n\n", total_processed);

        printf("Top %d Movies by Revenue:\n", MAX_TOP);
        for (int i = 0; i < nGlobalRev; i++)
            printf("%2d. \"%s\" | Revenue: %.2f\n", i+1, globalTopRev[i].title, globalTopRev[i].revenue);

        printf("\nTop %d Movies by Popularity:\n", MAX_TOP);
        for (int i = 0; i < nGlobalPop; i++)
            printf("%2d. \"%s\" | Year:%d | Popularity: %.2f\n",
                   i+1, globalTopPop[i].title, globalTopPop[i].year, globalTopPop[i].popularity);

        printf("\nAverage Rating per Year:\nYear  Count  Avg\n");
        for (int y = FIRST_YEAR; y <= LAST_YEAR; y++) {
            int idx = y - FIRST_YEAR;
            if (countYear_global[idx] > 0)
                printf("%4d  %6lld  %.3f\n",
                       y, countYear_global[idx], sumYear_global[idx]/countYear_global[idx]);
        }

        printf("\nAverage Rating per Genre:\n");
        printf("Genre                 Count    Avg\n");
        for (int i = 0; i < genreUsed_global; i++) {
            printf("%-20s %6lld   %.3f\n",
                   genreNames_global[i], genreCnt_global[i],
                   genreSum_global[i]/genreCnt_global[i]);
        }

        printf("\nCommunication Time: %.6f s\n", comm_time);
        printf("Computation Time: %.6f s\n", comp_time);
        printf("Total Execution Time: %.6f s\n", total_end - total_start);
        printf("=====================================\n");

        if (gather_topRev) free(gather_topRev);
        if (gather_topPop) free(gather_topPop);
        if (all_genres_packed) free(all_genres_packed);
        if (recv_pack_bytes) free(recv_pack_bytes);
        if (genre_displs) free(genre_displs);
        if (sumYear_global) free(sumYear_global);
        if (countYear_global) free(countYear_global);
    }

    free(recvcounts);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
