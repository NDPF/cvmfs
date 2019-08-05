/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_UTIL_ALGORITHM_H_
#define CVMFS_UTIL_ALGORITHM_H_

#include <sys/time.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#define BUFFSIZE 150
using namespace std; //NOLINT

#include "atomic.h"
#include "murmur.h"
// TODO(jblomer): should be also part of algorithm
#include "prng.h"
#include "util/single_copy.h"

#ifdef CVMFS_NAMESPACE_GUARD
namespace CVMFS_NAMESPACE_GUARD {
#endif


double DiffTimeSeconds(struct timeval start, struct timeval end);


/**
 * Knuth's random shuffle algorithm.
 */
template <typename T>
std::vector<T> Shuffle(const std::vector<T> &input, Prng *prng) {
  std::vector<T> shuffled(input);
  unsigned N = shuffled.size();
  // No shuffling for the last element
  for (unsigned i = 0; i < N; ++i) {
    const unsigned swap_idx = i + prng->Next(N - i);
    std::swap(shuffled[i], shuffled[swap_idx]);
  }
  return shuffled;
}


/**
 * Sorts the vector tractor and applies the same permutation to towed.  Both
 * vectors have to be of the same size.  Type T must be sortable (< operator).
 * Uses insertion sort (n^2), only efficient for small vectors.
 */
template <typename T, typename U>
void SortTeam(std::vector<T> *tractor, std::vector<U> *towed) {
  assert(tractor);
  assert(towed);
  assert(tractor->size() == towed->size());
  unsigned N = tractor->size();

  // Insertion sort on both, tractor and towed
  for (unsigned i = 1; i < N; ++i) {
    T val_tractor = (*tractor)[i];
    U val_towed = (*towed)[i];
    int pos;
    for (pos = i-1; (pos >= 0) && ((*tractor)[pos] > val_tractor); --pos) {
      (*tractor)[pos+1] = (*tractor)[pos];
      (*towed)[pos+1] = (*towed)[pos];
    }
    (*tractor)[pos+1] = val_tractor;
    (*towed)[pos+1] = val_towed;
  }
}


template <typename hashed_type>
struct hash_murmur {
  size_t operator() (const hashed_type key) const {
#ifdef __x86_64__
    return MurmurHash64A(&key, sizeof(key), 0x9ce603115bba659bLLU);
#else
    return MurmurHash2(&key, sizeof(key), 0x07387a4f);
#endif
  }
};


/**
 * Very simple StopWatch implementation. Currently the implementation does not
 * allow a restart of a stopped watch. You should always reset the clock before
 * you reuse it.
 *
 * Stopwatch watch();
 * watch.Start();
 * // do nasty thing
 * watch.Stop();
 * printf("%f", watch.GetTime());
 */
class StopWatch : SingleCopy {
 public:
  StopWatch() : running_(false) {}

  void Start();
  void Stop();
  void Reset();

  double GetTime() const;

 private:
  bool running_;
  timeval start_, end_;
};


class Log2Hist
{
 private:
  uint number_of_bins;
  atomic_int32 *bins;
  uint *boundary_values;
  uint count_digits(uint64_t n)
  {
    return (uint)floor(log10(n) + 1);
  }
  string generate_stars(uint n)
  {
    uint i = 0;
    string stars = "";
    for (i = 0; i < n; i++)
    {
      stars += "*";
    }
    return stars;
  }
  string to_string(uint n)
  {
    stringstream s;
    s << n;
    return s.str();
  }
 public:
  explicit Log2Hist(uint n);
  ~Log2Hist();
  void Add(float value)
  {
    uint i;
    uint flag = 1;

    for (i = 1; i <= this->number_of_bins; i++)
    {
      if (value < this->boundary_values[i])
      {
        atomic_inc32(&(this->bins[i]));
        flag = 0;
        return;
      }
    }
    if (flag)
    {
      atomic_inc32(&(this->bins[0]));  // add to overflow bin.
    }
  }
  atomic_int32 *GetBins();
  string Print();
  void PrintLog2Hist();
};

#ifdef CVMFS_NAMESPACE_GUARD
}  // namespace CVMFS_NAMESPACE_GUARD
#endif

#endif  // CVMFS_UTIL_ALGORITHM_H_
