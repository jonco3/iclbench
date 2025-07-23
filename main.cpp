#include <iostream>

#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

constexpr size_t Iterations = 10 * 1000 * 1000;

const char* sharedPath = "/data/local/tmp/ipbench-shared";
const size_t sharedSize = 4096;

struct SharedData {
  volatile uint64_t target = 0;
  volatile double result1 = 0.0;
  volatile double result2 = 0.0;
};
static_assert(sizeof(SharedData) <= sharedSize);

double runOneTest(int fd, void* data, size_t iterations, int cpu1, int cpu2);
double testLoop(SharedData* data, size_t iterations, int cpu, uint64_t start);
void setCPUAffinity(int cpu);

int main(int argc, char* argv[]) {
  // Set up shared memory region.

  int fd = open("/data/local/tmp/mysharedfile", O_CREAT | O_RDWR, 0666);
  if (fd == -1) {
    perror("open");
    return 1;
  }

  if (ftruncate(fd, sharedSize) == -1) {
    perror("ftruncate");
    return 1;
  }

  void* data = mmap(0, sharedSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  // Get number of processors.

  long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  if (nprocs == -1) {
    perror("nprocs");
    return 1;
  }
  printf("Number of CPU cores: %ld\n", nprocs);

  // Run the tests.

  for (int i = 0; i < nprocs; i++) {
    for (int j = 0; j < nprocs; j++) {
      double result = 0.0;
      if (i != j) {
        result = runOneTest(fd, data, Iterations, i, j);
      }
      printf("%8.2f ", result);
    }
    printf("\n");
  }

  return 0;
}

double runOneTest(int fd, void* data, size_t iterations, int cpu1, int cpu2) {
  // Init shared data.

  auto* sd = new (data) SharedData;

  // Fork child process.

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    exit(1);
  }

  bool isParent = pid != 0;
  int cpu = isParent ? cpu1 : cpu2;
  uint64_t start = isParent ? 1 : 2;
  auto* resultOut = isParent ? &sd->result1 : &sd->result2;

  // Run the test.

  *resultOut = testLoop(sd, iterations, cpu, start) / double(iterations);

  // Exit in the child.

  if (!isParent) {
    _exit(0);
  }

  // Wait for child in the parent.

  if (isParent) {
    int status;
    pid_t wpid = waitpid(pid, &status, 0);
    if (wpid == -1) {
      perror("waitpid");
      exit(1);
    } else {
      if (!WIFEXITED(status)) {
        fprintf(stderr, "Expected child to exit");
        exit(1);
      }
      if (WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Child exited with code %d", WEXITSTATUS(status));
        exit(1);
      }
    }
  }

  return std::min(sd->result1, sd->result2);
}

double testLoop(SharedData* data, size_t iterations, int cpu, uint64_t start) {
  // Latency test, based on:
  // https://github.com/ChipsandCheese/CnC-Tools/blob/main/CPU%20Tests/CoreCoherencyLatency/main.c#L131

  setCPUAffinity(cpu);

  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  uint64_t current = start;
  uint64_t end = 2 * iterations;
  while (current <= end) {
    if (__sync_bool_compare_and_swap(&data->target, current - 1, current)) {
      current += 2;
    }
  }

  struct timespec t1;
  clock_gettime(CLOCK_MONOTONIC, &t1);
  long seconds = t1.tv_sec - t0.tv_sec;
  long nanoseconds = t1.tv_nsec - t0.tv_nsec;
  double elapsed = seconds * 1e9 + nanoseconds;

  return elapsed;
}

void setCPUAffinity(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);

  // 0 = current thread/process
  if (sched_setaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_setaffinity");
    exit(1);
  }

  cpu_set_t setOut;
  CPU_ZERO(&setOut);
  if (sched_getaffinity(0, sizeof(setOut), &setOut) == -1) {
    perror("sched_getaffinity");
    exit(1);
  }

  if (memcmp(&set, &setOut, sizeof(set)) != 0) {
    fprintf(stderr, "sched_set/getaffinity don't match\n");
    exit(1);
  }
}
