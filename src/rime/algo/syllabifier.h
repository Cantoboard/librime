//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-7-12 Zou xu <zouivex@gmail.com>
//

#ifndef RIME_SYLLABIFIER_H_
#define RIME_SYLLABIFIER_H_

#include <stdint.h>
#include <rime_api.h>
#include "spelling.h"

namespace rime {

class Prism;
class Corrector;

using SyllableId = int32_t;

struct EdgeProperties : SpellingProperties {
  EdgeProperties(SpellingProperties sup): SpellingProperties(sup) {};
  EdgeProperties() = default;
  bool is_correction = false;
};

using SpellingMap = map<SyllableId, EdgeProperties>;
using VertexMap = map<size_t, SpellingType>;
using EndVertexMap = map<size_t, SpellingMap>;
using EdgeMap = map<size_t, EndVertexMap>;

using SpellingPropertiesList = vector<const EdgeProperties*>;
using SpellingIndex = hash_map<SyllableId, SpellingPropertiesList>;
using SpellingIndices = vector<SpellingIndex>;

struct SyllableGraph {
  size_t input_length = 0;
  size_t interpreted_length = 0;
  VertexMap vertices;
  EdgeMap edges;
  SpellingIndices indices;
};

class DictEntryList;
using WordGraph = map<int, map<int, DictEntryList>>;

struct SearchState;

struct SearchContext {
  string input, prev_input;
  size_t incremental_search_from_pos;
  WordGraph prev_words;
  an<SearchState> prev_search_state;
  template<typename T>
  static void RemoveStateEntriesFromGraph(map<int, map<int, T>>& graph, size_t cache_valid_len) {
    for (auto it_by_start_pos = graph.begin(); it_by_start_pos != graph.end();) {
      size_t start_pos = it_by_start_pos->first;
#ifdef DEBUG
      LOG(ERROR) << "RemoveStateEntriesFromGraph start_pos " << start_pos << " cache_valid_len " << cache_valid_len;
#endif
      if (start_pos > cache_valid_len) {
#ifdef DEBUG
      LOG(ERROR) << "Removing row " << start_pos;
#endif
        it_by_start_pos = graph.erase(it_by_start_pos);
      } else {
        auto& state_same_start_pos = it_by_start_pos->second;
        for (auto it_by_end_pos = state_same_start_pos.begin(); it_by_end_pos != state_same_start_pos.end();) {
          size_t end_pos = it_by_end_pos->first;
          if (end_pos > cache_valid_len) {
#ifdef DEBUG
            LOG(ERROR) << "Removing entry " << start_pos << "," << end_pos;
#endif
            it_by_end_pos = state_same_start_pos.erase(it_by_end_pos);
          } else {
            it_by_end_pos++;
          }
        }
        it_by_start_pos++;
      }
    }
  }
};

class Syllabifier {
 public:
  Syllabifier() = default;
  explicit Syllabifier(const string &delimiters,
                       bool enable_completion = false,
                       bool strict_spelling = false)
      : delimiters_(delimiters),
        enable_completion_(enable_completion),
        strict_spelling_(strict_spelling) {
  }

  RIME_API int BuildSyllableGraph(const string &input,
                                  Prism &prism,
                                  SyllableGraph *graph);
  RIME_API void EnableCorrection(Corrector* corrector);

 protected:
  void CheckOverlappedSpellings(SyllableGraph *graph,
                                size_t start, size_t end);
  void Transpose(SyllableGraph* graph);

  string delimiters_;
  bool enable_completion_ = false;
  bool strict_spelling_ = false;
  Corrector* corrector_ = nullptr;
};

}  // namespace rime

#endif  // RIME_SYLLABIFIER_H_
