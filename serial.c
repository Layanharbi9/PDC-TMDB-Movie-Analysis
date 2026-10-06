#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

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

void trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
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
    strcpy(s, temp);
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
        arr[pos].title[255]='\0';
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
    arr[pos].title[255]='\0';
    arr[pos].year = year;
    arr[pos].revenue = rev;
    arr[pos].popularity = pop;
    arr[pos].score = score;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: %s <dataset.csv>\n", argv[0]);
        return 1;
    }

    clock_t start = clock();

    FILE *fp = fopen(argv[1], "r");
    if (!fp) { perror("File"); return 1; }

    char line[MAX_LINE];
    char *fields[MAX_FIELDS];

    fgets(line, sizeof(line), fp);

    int idxTitle=-1, idxRating=-1, idxRelease=-1, idxRev=-1;
    int idxBudget=-1, idxPop=-1, idxGenre=-1;

    int numFields = splitCSV(line, fields);
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

    long long total = 0;

    double sumYear[YEAR_RANGE] = {0};
    long long countYear[YEAR_RANGE] = {0};

    char genreNames[MAX_GENRES][64];
    double genreSum[MAX_GENRES] = {0};
    long long genreCnt[MAX_GENRES] = {0};
    int genreUsed = 0;

    TopMovie topRev[MAX_TOP] = {0};
    TopMovie topPop[MAX_TOP] = {0};
    int nTopRev = 0, nTopPop = 0;

    while (fgets(line, sizeof(line), fp)) {

        int fc = splitCSV(line, fields);
        if (fc < numFields) continue;

        total++;

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
            sumYear[year-FIRST_YEAR] += rating;
            countYear[year-FIRST_YEAR]++;
        }

        if (idxGenre>=0 && rating>0) {
            char tmp[256];
            strncpy(tmp, fields[idxGenre],255);
            tmp[255]='\0';
            cleanGenre(tmp);

            char *first = strtok(tmp,",");
            if (first) {
                cleanGenre(first);
                trim(first);

                if (strlen(first)>0) {
                    first[0] = toupper((unsigned char)first[0]);
                    for (int i = 1; first[i]; i++)
                        first[i] = tolower((unsigned char)first[i]);

                    int found = -1;

                    for (int g=0; g<genreUsed; g++) {
                        if (!strcasecmp(genreNames[g], first)) {
                            found = g;
                            break;
                        }
                    }
                    if (found==-1 && genreUsed<MAX_GENRES) {
                        strncpy(genreNames[genreUsed], first,63);
                        genreNames[genreUsed][63]='\0';
                        found = genreUsed++;
                    }

                    genreSum[found] += rating;
                    genreCnt[found]++;
                }
            }
        }

        insertTop(topRev, &nTopRev, title, year, rev, pop, rev);
        insertTop(topPop, &nTopPop, title, year, rev, pop, pop);
    }

    fclose(fp);

    printf("=====================================\n");
    printf("Serial Version – TMDB Movie Analysis\n");
    printf("Movies processed: %lld\n\n", total);

    printf("Top %d Movies by Revenue:\n", MAX_TOP);
    for (int i = 0; i < nTopRev; i++)
        printf("%2d. \"%s\" | Revenue: %.2f\n", i+1, topRev[i].title, topRev[i].revenue);

    printf("\nTop %d Movies by Popularity:\n", MAX_TOP);
    for (int i = 0; i < nTopPop; i++)
        printf("%2d. \"%s\" | Year:%d | Popularity: %.2f\n",
               i+1, topPop[i].title, topPop[i].year, topPop[i].popularity);

    printf("\nAverage Rating per Year:\nYear  Count  Avg\n");
    for (int y = FIRST_YEAR; y <= LAST_YEAR; y++) {
        int idx = y - FIRST_YEAR;
        if (countYear[idx] > 0)
            printf("%4d  %6lld  %.3f\n",
                   y, countYear[idx], sumYear[idx]/countYear[idx]);
    }

    printf("\nAverage Rating per Genre:\n");
    printf("Genre                 Count    Avg\n");
    for (int i = 0; i < genreUsed; i++) {
        printf("%-20s %6lld   %.3f\n",
               genreNames[i], genreCnt[i],
               genreSum[i]/genreCnt[i]);
    }

clock_t end = clock();
double time_taken = (double)(end - start) / CLOCKS_PER_SEC;
printf("Execution Time: %.6f s\n", time_taken);

    return 0;
}

