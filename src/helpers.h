#include "pallas/pallas.h"
#include "pallas/pallas_read.h"
#include "pallas/pallas_timestamp.h"
#include <string>
#include <vector>

#define pallas_assert(cond, errmsg) if (!(cond)) panic(errmsg)

#define GREEN_MAX_DEVIATION 1.
#define YELLOW_MAX_DEVIATION 10.

void panic(const std::string& errmsg);

class Histogram {
public:
  Histogram(pallas::ThreadReader *tr, pallas::Token token, size_t nvalues);
  Histogram(Histogram &&) = default;
  Histogram(const Histogram &) = default;
  Histogram &operator=(Histogram &&) = default;
  Histogram &operator=(const Histogram &) = default;
  ~Histogram();

  pallas_duration_t min_duration;
  pallas_duration_t max_duration;
  pallas_duration_t timestep;
  std::vector<size_t> values;
};

pallas_duration_t getTokenDuration(pallas::ThreadReader *thread_reader, pallas::Token token);

double getLineColor(pallas::ThreadReader *thread_reader);
