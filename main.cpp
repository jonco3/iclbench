#include <iostream>

#include <fcntl.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char* sharedPath = "/data/local/tmp/ipbench-shared";
const size_t sharedSize = 4096;

struct Options {
  size_t iterations = 10 * 1000 * 1000;
};

struct SyncCASTest {
  volatile uint64_t target = 0;
  void run(size_t iterations, bool isParent);
};

struct Results {
  volatile double result1 = 0.0;
  volatile double result2 = 0.0;
};

bool parseArgs(int argc, char* argv[], Options& options);
void printUsage();
template <typename Test>
double runOneTest(int fd, void* data, size_t iterations, int cpu1, int cpu2);
void setCPUAffinity(int cpu);

int main(int argc, char* argv[]) {
  // Select line buffered output.

  setvbuf(stdout, nullptr, _IOLBF, 1024);

  // Parse command line options.

  Options options;
  if (!parseArgs(argc, argv, options)) {
    printUsage();
    return 1;
  }

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
        result = runOneTest<SyncCASTest>(fd, data, options.iterations, i, j);
      }
      printf("%8.2f ", result);
    }
    printf("\n");
  }

  return 0;
}

bool parseArgs(int argc, char* argv[], Options& options) {
  int i = 1;
  while (i < argc) {
    const char* arg = argv[i++];

    if (strcmp(arg, "-i") == 0) {
      if (i == argc) {
        return false;
      }
      arg = argv[i++];
      if (sscanf(arg, "%zu", &options.iterations) != 1) {
        return false;
      }
      printf("Iterations: %zu\n", options.iterations);
    } else {
      return false;
    }
  }

  return true;
}

void printUsage() {
  fprintf(stderr, "usage: iclbench [-i ITERATIONS]\n");
}

template <typename Test>
double runOneTest(int fd, void* data, size_t iterations, int cpu1, int cpu2) {
  // Init shared data.

  if (sizeof(Test) + sizeof(Results) >= sharedSize) {
    fprintf(stderr, "Test data too large.\n");
    exit(1);
  }

  auto* test = new (data) Test;
  auto* results = new (test + 1) Results;

  // Fork child process.

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    exit(1);
  }

  bool isParent = pid != 0;

  // Ensure the child gets killed if the parent dies.

  if (!isParent) {
    prctl(PR_SET_PDEATHSIG, SIGHUP);
    if (getppid() == 1) {
      exit(1);
    }
  }

  // Bind parent/child process to the requested CPU.

  int cpu = isParent ? cpu1 : cpu2;
  setCPUAffinity(cpu);

  // Run the test.

  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  test->run(iterations, isParent);

  struct timespec t1;
  clock_gettime(CLOCK_MONOTONIC, &t1);
  long seconds = t1.tv_sec - t0.tv_sec;
  long nanoseconds = t1.tv_nsec - t0.tv_nsec;
  double elapsed = seconds * 1e9 + nanoseconds;
  auto* resultOut = isParent ? &results->result1 : &results->result2;
  *resultOut = elapsed / double(iterations);

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

  return std::min(results->result1, results->result2);
}

void SyncCASTest::run(size_t iterations, bool isParent) {
  // Latency test, based on:
  // https://github.com/ChipsandCheese/CnC-Tools/blob/main/CPU%20Tests/CoreCoherencyLatency/main.c#L131

  uint64_t start = isParent ? 1 : 2;
  uint64_t current = start;
  uint64_t end = 2 * iterations;
  while (current <= end) {
    if (__sync_bool_compare_and_swap(&target, current - 1, current)) {
      current += 2;
    }
  }
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
