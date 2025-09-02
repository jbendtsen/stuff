#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

long long getUserTime(HANDLE proc) {
    FILETIME creationTime, exitTime, kernelDurationFt, userDurationFt;
    GetProcessTimes(proc, &creationTime, &exitTime, &kernelDurationFt, &userDurationFt);

    return ((long long)userDurationFt.dwHighDateTime << 32) |
        ((long long)userDurationFt.dwLowDateTime & 0xFFFFffffLL);
}

int main(int argc, char **argv) {
    HANDLE curProcess = GetCurrentProcess();

    LARGE_INTEGER wallStart, wallEnd, wallFreq;
    QueryPerformanceFrequency(&wallFreq);
    QueryPerformanceCounter(&wallStart);

    long long userStart = getUserTime(curProcess);

    long long count = 100000LL;
    if (argc >= 2) {
        count = strtoull(argv[1], NULL, 0);
    }

    long long nPrimes = 1; // two is a prime number, but we start testing from three
    for (long long i = 3; i < count; i++) {
        long long j = 2;
        while (j < i) {
            if (i % j == 0) {
                break;
            }
            j++;
        }
        if (j == i) {
            nPrimes++;
        }
    }

    QueryPerformanceCounter(&wallEnd);

    long long userEnd = getUserTime(curProcess);

    printf("number of primes less than %lld: %lld\n", count, nPrimes);

    double dWall = (double)(wallEnd.QuadPart - wallStart.QuadPart);
    if (wallFreq.QuadPart > 0) {
        dWall /= (double)wallFreq.QuadPart;
    }

    double dUser = (double)(userEnd - userStart) / (double)(10 * 1000 * 1000);

    printf("wall time: %g secs\n", dWall);
    printf("user time: %g secs\n", dUser);

    ULONG64 cycleTime;
    QueryProcessCycleTime(curProcess, &cycleTime);

    printf("cycles: %lld\n", cycleTime);
}
