#include "predictor_engines.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t ALLOCATION_ENTRIES = 64;
constexpr uint32_t SAMPLE_ENTRIES = 64;
constexpr uint32_t SANDBOX_SETS = 128;
constexpr uint32_t SANDBOX_WAYS = 4;
constexpr uint32_t SANDBOX_ENTRIES = SANDBOX_SETS * SANDBOX_WAYS;
constexpr uint32_t PREDICTOR_COUNT = 3;
constexpr uint32_t EPOCH_DEMANDS = 100;
constexpr uint32_t DEAD_LIMIT = 150;
constexpr int8_t MAX_IB = -8;
constexpr int8_t MAX_IA = 5;
constexpr uint32_t CONSERVATIVE_DEGREE = 3;
constexpr double PROFICIENCY_BOUNDARY = 0.75;
constexpr double DEFICIENCY_BOUNDARY = 0.05;

enum class Kind : uint8_t { UI, IA, IB };

struct AllocState {
  Kind kind = Kind::UI;
  int8_t level = 0;
};

struct AllocationEntry {
  uint64_t pc = 0;
  std::array<AllocState, PREDICTOR_COUNT> state{};
  bool valid = false;
};

struct SampleEntry {
  uint64_t pc = 0;
  uint16_t demands = 0;
  uint8_t dead = 0;
  std::array<uint64_t, PREDICTOR_COUNT> issued{};
  std::array<uint64_t, PREDICTOR_COUNT> confirmed{};
  bool valid = false;
};

struct SandboxEntry {
  uint64_t line = 0;
  uint64_t owner_pc = 0;
  uint32_t token = 0;
  uint8_t predictor = 0;
  bool resident_l1 = false;
  bool valid = false;
};

std::array<std::array<AllocationEntry, ALLOCATION_ENTRIES>, NUM_CPUS> allocations{};
std::array<std::array<SampleEntry, SAMPLE_ENTRIES>, NUM_CPUS> samples{};
std::array<std::array<SandboxEntry, SANDBOX_ENTRIES>, NUM_CPUS> sandbox{};
std::array<std::array<uint8_t, SANDBOX_SETS>, NUM_CPUS> sandbox_replace{};
std::array<uint32_t, NUM_CPUS> next_sandbox_token{};

uint32_t pc_index(uint64_t pc, uint32_t size)
{
  return static_cast<uint32_t>((pc ^ (pc >> 11) ^ (pc >> 23)) % size);
}

uint32_t line_set(uint64_t line)
{
  return static_cast<uint32_t>((line ^ (line >> 9) ^ (line >> 19)) % SANDBOX_SETS);
}

AllocationEntry& allocation(uint32_t cpu, uint64_t pc)
{
  AllocationEntry& entry = allocations[cpu][pc_index(pc, ALLOCATION_ENTRIES)];
  if (!entry.valid || entry.pc != pc) {
    entry = AllocationEntry{};
    entry.pc = pc;
    entry.valid = true;
  }
  return entry;
}

SampleEntry& sample(uint32_t cpu, uint64_t pc)
{
  SampleEntry& entry = samples[cpu][pc_index(pc, SAMPLE_ENTRIES)];
  if (!entry.valid || entry.pc != pc) {
    entry = SampleEntry{};
    entry.pc = pc;
    entry.valid = true;
  }
  return entry;
}

uint32_t degree_for(const AllocState& state)
{
  return state.kind == Kind::IA
             ? CONSERVATIVE_DEGREE + static_cast<uint32_t>(state.level) + 1
             : CONSERVATIVE_DEGREE;
}

SandboxEntry* find_sandbox(uint32_t cpu, uint64_t line)
{
  const uint32_t base = line_set(line) * SANDBOX_WAYS;
  for (uint32_t way = 0; way < SANDBOX_WAYS; ++way) {
    SandboxEntry& entry = sandbox[cpu][base + way];
    if (entry.valid && entry.line == line) return &entry;
  }
  return nullptr;
}

SandboxEntry& allocate_sandbox(uint32_t cpu, uint64_t line)
{
  const uint32_t set = line_set(line);
  const uint32_t base = set * SANDBOX_WAYS;
  for (uint32_t way = 0; way < SANDBOX_WAYS; ++way)
    if (!sandbox[cpu][base + way].valid) return sandbox[cpu][base + way];
  const uint32_t victim = sandbox_replace[cpu][set] % SANDBOX_WAYS;
  sandbox_replace[cpu][set] = static_cast<uint8_t>((victim + 1) % SANDBOX_WAYS);
  return sandbox[cpu][base + victim];
}

bool issue_one(CACHE* cache, uint64_t pc, uint64_t base_addr,
               uint64_t line, uint32_t predictor, uint32_t distance)
{
  if (line > (std::numeric_limits<uint64_t>::max() >> LOG2_BLOCK_SIZE))
    return false;
  const uint64_t address = line << LOG2_BLOCK_SIZE;
  if ((address >> LOG2_PAGE_SIZE) != (base_addr >> LOG2_PAGE_SIZE) ||
      find_sandbox(cache->cpu, line) != nullptr)
    return false;

  const int fill = distance < CONSERVATIVE_DEGREE ? FILL_L1 : FILL_L2;
  uint32_t token = ++next_sandbox_token[cache->cpu];
  if (token == 0) token = ++next_sandbox_token[cache->cpu];
  if (!cache->prefetch_line(pc, base_addr, address, fill, token)) return false;

  SandboxEntry& entry = allocate_sandbox(cache->cpu, line);
  entry = SandboxEntry{};
  entry.line = line;
  entry.owner_pc = pc;
  entry.token = token;
  entry.predictor = static_cast<uint8_t>(predictor);
  entry.valid = true;

  SampleEntry& stats = sample(cache->cpu, pc);
  if (stats.issued[predictor] != std::numeric_limits<uint64_t>::max())
    ++stats.issued[predictor];
  return true;
}

bool issue_requests(CACHE* cache, uint64_t pc, uint64_t addr,
                    uint32_t predictor,
                    const std::vector<predictor_engines::Request>& requests)
{
  bool issued = false;
  for (const predictor_engines::Request& request : requests) {
    if (issue_one(cache, pc, addr, request.line, predictor, request.distance))
      issued = true;
  }
  return issued;
}

void transition_epoch(uint32_t cpu, uint64_t pc)
{
  SampleEntry& stats = sample(cpu, pc);
  if (++stats.demands < EPOCH_DEMANDS) return;
  stats.demands = 0;

  AllocationEntry& table = allocation(cpu, pc);
  const std::array<AllocState, PREDICTOR_COUNT> previous = table.state;
  std::array<double, PREDICTOR_COUNT> accuracy{};
  std::array<bool, PREDICTOR_COUNT> had_issue{};
  for (uint32_t p = 0; p < PREDICTOR_COUNT; ++p) {
    had_issue[p] = stats.issued[p] != 0;
    accuracy[p] = had_issue[p]
                      ? static_cast<double>(stats.confirmed[p]) / stats.issued[p]
                      : 0.0;
  }

  bool promoted = false;
  for (uint32_t p = 0; p < PREDICTOR_COUNT; ++p) {
    if (previous[p].kind == Kind::UI && had_issue[p] &&
        accuracy[p] >= PROFICIENCY_BOUNDARY) {
      table.state[p].kind = Kind::IA;
      table.state[p].level = 0;
      promoted = true;
    }
  }

  for (uint32_t p = 0; p < PREDICTOR_COUNT; ++p) {
    const AllocState& old = previous[p];
    AllocState& current = table.state[p];
    if (old.kind == Kind::UI && current.kind == Kind::UI) {
      if (promoted) {
        current.kind = Kind::IB;
        current.level = 0;
      } else if (had_issue[p] && accuracy[p] < DEFICIENCY_BOUNDARY) {
        current.kind = Kind::IB;
        current.level = MAX_IB;
      }
    } else if (old.kind == Kind::IA) {
      if (accuracy[p] >= PROFICIENCY_BOUNDARY) {
        if (current.level < MAX_IA) ++current.level;
      } else if (old.level == 0) {
        current.kind = Kind::UI;
        current.level = 0;
      } else {
        --current.level;
      }
    } else if (old.kind == Kind::IB && old.level < 0) {
      current.level = static_cast<int8_t>(old.level + 1);
    }
  }

  bool any_ia = false;
  for (const AllocState& state : table.state)
    any_ia |= state.kind == Kind::IA;
  if (!any_ia) {
    for (AllocState& state : table.state) {
      if (state.kind == Kind::IB && state.level == 0) {
        state.kind = Kind::UI;
        state.level = 0;
      }
    }
  }
}

void update_dead_counter(uint32_t cpu, uint64_t pc, bool issued)
{
  AllocationEntry& table = allocation(cpu, pc);
  SampleEntry& stats = sample(cpu, pc);
  bool any_ia = false;
  for (const AllocState& state : table.state)
    any_ia |= state.kind == Kind::IA;

  if (issued) {
    if (stats.dead > 0) --stats.dead;
  } else if (any_ia && stats.dead < DEAD_LIMIT) {
    ++stats.dead;
  }

  if (stats.dead >= DEAD_LIMIT) {
    table.state = {};
    stats.issued = {};
    stats.confirmed = {};
    stats.demands = 0;
    stats.dead = 0;
  }
}
}

void CACHE::l1d_prefetcher_initialize()
{
  allocations[cpu] = {};
  samples[cpu] = {};
  sandbox[cpu] = {};
  sandbox_replace[cpu] = {};
  next_sandbox_token[cpu] = 0;
  predictor_engines::initialize_ip_stride(cpu);
  predictor_engines::initialize_spp(cpu);
  predictor_engines::initialize_global_stream(cpu);
  std::cout << "Alecto L1D prefetcher: IP stride, SPP, global stream\n";
}

void CACHE::l1d_prefetcher_operate(uint64_t addr, uint64_t ip,
                                  uint8_t cache_hit, uint8_t type,
                                  uint8_t critical_ip_flag)
{
  if (type == PREFETCH) return;

  const uint64_t line = addr >> LOG2_BLOCK_SIZE;
  SampleEntry& stats = sample(cpu, ip);
  SandboxEntry* observed = find_sandbox(cpu, line);
  if (observed != nullptr && observed->owner_pc == ip) {
    if (stats.confirmed[observed->predictor] !=
        std::numeric_limits<uint64_t>::max())
      ++stats.confirmed[observed->predictor];
    observed->valid = false;
  }

  AllocationEntry& table = allocation(cpu, ip);
  std::array<bool, PREDICTOR_COUNT> issued{};
  for (uint32_t p = 0; p < PREDICTOR_COUNT; ++p) {
    const AllocState& state = table.state[p];
    if (state.kind == Kind::IB) continue;
    const uint32_t degree = degree_for(state);
    std::vector<predictor_engines::Request> requests;
    if (p == 0)
      requests = predictor_engines::ip_stride(cpu, addr, ip, degree);
    else if (p == 1)
      requests = predictor_engines::spp(cpu, addr, ip, degree);
    else
      requests = predictor_engines::global_stream(cpu, addr, degree);
    issued[p] = issue_requests(this, ip, addr, p, requests);
  }

  const bool any_issued = issued[0] || issued[1] || issued[2];
  update_dead_counter(cpu, ip, any_issued);
  transition_epoch(cpu, ip);
  (void)cache_hit;
  (void)critical_ip_flag;
}

void CACHE::l1d_prefetcher_cache_fill(uint64_t v_addr, uint64_t addr,
                                     uint32_t set, uint32_t way,
                                     uint8_t prefetch,
                                     uint64_t v_evicted_addr,
                                     uint64_t evicted_addr,
                                     uint32_t metadata_in)
{
  if (v_evicted_addr != 0) {
    SandboxEntry* evicted = find_sandbox(cpu, evicted_addr >> LOG2_BLOCK_SIZE);
    if (evicted != nullptr) evicted->resident_l1 = false;
  }

  SandboxEntry* entry = find_sandbox(cpu, addr >> LOG2_BLOCK_SIZE);
  if (entry == nullptr) return;
  if (prefetch && entry->token != metadata_in) return;
  entry->resident_l1 = true;
}

void CACHE::l1d_prefetcher_notify_about_dtlb_eviction(
    uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch,
    uint64_t evicted_addr, uint32_t metadata_in) {}

void CACHE::l1d_prefetcher_final_stats()
{
  std::cout << "Alecto candidates: IP stride, SPP, global stream\n";
}
