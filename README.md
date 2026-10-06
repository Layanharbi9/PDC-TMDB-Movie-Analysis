# 🎬 TMDB Movie Analysis — Serial vs. OpenMP vs. MPI vs. GPU

**Parallel & Distributed Computing (PDC) final project** — analyzing **1M+ movie records** from the TMDB dataset and measuring how far parallelism can take the same workload.

| Approach | File | Model |
|---|---|---|
| Serial baseline | `serial.c` | single core |
| Shared memory | `parallel.c` | OpenMP · `schedule(runtime)` — static / dynamic / guided |
| Distributed, blocking | `mpi_blocking_row.c` | MPI `Send/Recv` · row distribution |
| Distributed, non-blocking | `mpi_non-blocking_row.c` | MPI `Isend/Irecv` · overlap compute & communication |
| GPU | `gpu_*.py`, `GPU_*.ipynb` | GPU serial vs. parallel, GPU + MPI |

**What was measured:** execution time, speedup, efficiency, strong & weak scaling, and computation vs. communication overhead — **75 OpenMP configurations** (5 chunk sizes × 3 schedules × 5 thread counts) plus MPI runs on 2–8 processes.

**Tech:** C · OpenMP · MPI · Python · Jupyter · Make

## 📦 Dataset (not included — too large for GitHub)

Download **Full TMDB Movies Dataset 2024 (1M Movies)** from
[Kaggle](https://www.kaggle.com/datasets/asaniczka/tmdb-movies-dataset-2023-930k-movies) and place
`TMDB_movie_dataset_v112.csv` in the project folder. The weak-scaling subsets
(`TMDB_25.csv`, `TMDB_50.csv`, `TMDB_75.csv`, `TMDB_100.csv`) are 25 / 50 / 75 / 100 % samples of it.

## ⚡ Quick start

```bash
make                       # builds serial, parallel, mpi_blocking_row, mpi_non-blocking_row
./serial TMDB_movie_dataset_v112.csv
OMP_NUM_THREADS=8 OMP_SCHEDULE="dynamic,256" ./parallel TMDB_movie_dataset_v112.csv
mpirun -np 4 ./mpi_non-blocking_row TMDB_movie_dataset_v112.csv
```

## 👩‍💻 Author

**Layan Mohammed Alharbi** — Umm Al-Qura University, Faculty of Computing

---

# Full project documentation

This project focuses on analyzing a large-scale movie dataset using three computational approaches:
- Serial implementation (`serial.c`)
- OpenMP shared-memory implementation (`parallel.c`)
- MPI distributed-memory implementations:
  - Blocking (`mpi_blocking_row.c`)
  - Non-blocking (`mpi_non-blocking_row.c`)

The objective of this project is to evaluate and compare performance based on:
- Execution time
- Speedup and efficiency
- Strong and weak scaling behavior
- Computation vs. communication overhead

---

## Dataset

- Dataset name: Full TMDB Movies Dataset (2024) – 1M Movies  
- Source: https://www.kaggle.com/datasets/asaniczka/tmdb-movies-dataset-2023-930k-movies  
- File size: ~577 MB  

Files used in this project:
- Main full dataset: `TMDB_movie_dataset_v112.csv` (for serial, OpenMP, and strong scaling)  
- Sampled subsets for weak scaling:
  - `TMDB_25.csv`
  - `TMDB_50.csv`
  - `TMDB_75.csv`
  - `TMDB_100.csv`

All files were placed inside the project directory:

PDC_MovieAnalysis_Project/TMDB_movie_dataset_v112.csv  
PDC_MovieAnalysis_Project/TMDB_25.csv  
PDC_MovieAnalysis_Project/TMDB_50.csv  
PDC_MovieAnalysis_Project/TMDB_75.csv  
PDC_MovieAnalysis_Project/TMDB_100.csv  

---

## Build Instructions (Makefile)

A Makefile is used to compile all source files.

To compile all programs:  
make  

To compile specific executables:  
make serial  
make parallel  
make mpi_blocking_row  
make mpi_non-blocking_row  

This generates the following executables:  
serial  
parallel  
mpi_blocking_row  
mpi_non-blocking_row  

---

## Run Instructions

### 1) Serial implementation (serial.c)

The serial version was executed three times using the full dataset:

./serial TMDB_movie_dataset_v112.csv  
./serial TMDB_movie_dataset_v112.csv  
./serial TMDB_movie_dataset_v112.csv  

The **average execution time** was then computed and used as the baseline for comparison.

---

### 2) OpenMP implementation (parallel.c)

The OpenMP code uses `schedule(runtime)`, so the scheduling policy and chunk size are set using environment variables.

General execution format:

export OMP_NUM_THREADS=<threads>  
export OMP_SCHEDULE="<SCHEDULE>,<chunk>"  
./parallel TMDB_movie_dataset_v112.csv  

Example configuration actually used:

export OMP_NUM_THREADS=2  
export OMP_SCHEDULE="static,64"  
./parallel TMDB_movie_dataset_v112.csv  

Parameters tested:

Threads: 2, 4, 8, 16, 20  
Schedules: static, dynamic, guided  
Chunk sizes: 64, 128, 256, 512, 1024  

In total, 75 configurations were evaluated (5 chunk sizes × 3 schedules × 5 thread counts).  
For each configuration, the following metrics were recorded:
- Execution time  
- Average time (over multiple runs when needed)  
- Speedup (relative to the serial baseline)  
- Parallel efficiency  

---

### 3) MPI blocking implementation (mpi_blocking_row.c)

#### Strong scaling

Strong scaling experiments were performed using the full dataset while varying only the number of processes:

mpirun -np 2 ./mpi_blocking_row TMDB_movie_dataset_v112.csv  
mpirun -np 4 ./mpi_blocking_row TMDB_movie_dataset_v112.csv  
mpirun -np 6 ./mpi_blocking_row TMDB_movie_dataset_v112.csv  
mpirun -np 8 ./mpi_blocking_row TMDB_movie_dataset_v112.csv  

Each configuration was executed multiple times, and the **average** of the following metrics was computed:
- Computation time  
- Communication time  
- Total execution time  

---

#### Weak scaling

For weak scaling with the blocking MPI implementation, four different subset files were used to represent different data sizes:

TMDB_25.csv  
TMDB_50.csv  
TMDB_75.csv  
TMDB_100.csv  

For each process count (2, 4, 6, 8), the program was executed once with each of these files.  
The command patterns used were as follows:

For 2 processes:

mpirun -np 2 ./mpi_blocking_row TMDB_25.csv  
mpirun -np 2 ./mpi_blocking_row TMDB_50.csv  
mpirun -np 2 ./mpi_blocking_row TMDB_75.csv  
mpirun -np 2 ./mpi_blocking_row TMDB_100.csv  

For 4 processes:

mpirun -np 4 ./mpi_blocking_row TMDB_25.csv  
mpirun -np 4 ./mpi_blocking_row TMDB_50.csv  
mpirun -np 4 ./mpi_blocking_row TMDB_75.csv  
mpirun -np 4 ./mpi_blocking_row TMDB_100.csv  

For 6 processes:

mpirun -np 6 ./mpi_blocking_row TMDB_25.csv  
mpirun -np 6 ./mpi_blocking_row TMDB_50.csv  
mpirun -np 6 ./mpi_blocking_row TMDB_75.csv  
mpirun -np 6 ./mpi_blocking_row TMDB_100.csv  

For 8 processes:

mpirun -np 8 ./mpi_blocking_row TMDB_25.csv  
mpirun -np 8 ./mpi_blocking_row TMDB_50.csv  
mpirun -np 8 ./mpi_blocking_row TMDB_75.csv  
mpirun -np 8 ./mpi_blocking_row TMDB_100.csv  

For each configuration, runs were repeated and the **average** values of:
- Computation time  
- Communication time  
- Total execution time  
were computed and analyzed.

---

### 4) MPI non-blocking implementation (mpi_non-blocking_row.c)

#### Strong scaling

Strong scaling for the non-blocking version was also performed using the full dataset:

mpirun -np 2 ./mpi_non-blocking_row TMDB_movie_dataset_v112.csv  
mpirun -np 4 ./mpi_non-blocking_row TMDB_movie_dataset_v112.csv  
mpirun -np 6 ./mpi_non-blocking_row TMDB_movie_dataset_v112.csv  
mpirun -np 8 ./mpi_non-blocking_row TMDB_movie_dataset_v112.csv  

Each configuration was executed multiple times, and the **average**:
- Computation time  
- Communication time  
- Total execution time  
was recorded.

---

#### Weak scaling

For weak scaling with the non-blocking MPI implementation, the same subset files were used:

TMDB_25.csv  
TMDB_50.csv  
TMDB_75.csv  
TMDB_100.csv  

For each process count (2, 4, 6, 8), the program was executed with each of these files using the following pattern:

For 2 processes:

mpirun -np 2 ./mpi_non-blocking_row TMDB_25.csv  
mpirun -np 2 ./mpi_non-blocking_row TMDB_50.csv  
mpirun -np 2 ./mpi_non-blocking_row TMDB_75.csv  
mpirun -np 2 ./mpi_non-blocking_row TMDB_100.csv  

For 4 processes:

mpirun -np 4 ./mpi_non-blocking_row TMDB_25.csv  
mpirun -np 4 ./mpi_non-blocking_row TMDB_50.csv  
mpirun -np 4 ./mpi_non-blocking_row TMDB_75.csv  
mpirun -np 4 ./mpi_non-blocking_row TMDB_100.csv  

For 6 processes:

mpirun -np 6 ./mpi_non-blocking_row TMDB_25.csv  
mpirun -np 6 ./mpi_non-blocking_row TMDB_50.csv  
mpirun -np 6 ./mpi_non-blocking_row TMDB_75.csv  
mpirun -np 6 ./mpi_non-blocking_row TMDB_100.csv  

For 8 processes:

mpirun -np 8 ./mpi_non-blocking_row TMDB_25.csv  
mpirun -np 8 ./mpi_non-blocking_row TMDB_50.csv  
mpirun -np 8 ./mpi_non-blocking_row TMDB_75.csv  
mpirun -np 8 ./mpi_non-blocking_row TMDB_100.csv  

For each configuration, multiple runs were performed and the **average**:
- Computation time  
- Communication time  
- Total execution time  
was used in the analysis.

---

## Summary of work completed

- Serial implementation: executed three times using the full dataset; the average execution time was used as a baseline.  
- OpenMP implementation: 75 different configurations combining threads, schedule policies, and chunk sizes were tested; execution time, average time, speedup, and efficiency were analyzed.  
- MPI blocking implementation:  
  - Strong scaling using the full dataset with 2, 4, 6, and 8 processes, with average computation, communication, and total times computed.  
  - Weak scaling using four subset files (TMDB_25.csv, TMDB_50.csv, TMDB_75.csv, TMDB_100.csv) across 2, 4, 6, and 8 processes, with averages collected for all timing metrics.  
- MPI non-blocking implementation:  
  - Strong scaling using the full dataset with 2, 4, 6, and 8 processes; average computation, communication, and total times were recorded.  
  - Weak scaling using the same subset files across the same process counts, again using averaged timing values.  

This README documents the dataset, build process, run commands, and the experimental setup required to fully reproduce the results of the project.
