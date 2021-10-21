//
// Copyright RIME Developers
// Distributed under the BSD License
//
// Script translator
//
// 2011-07-10 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <stack>
#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <rime/composition.h>
#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>
#include <rime/algo/syllabifier.h>
#include <rime/dict/corrector.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/user_dictionary.h>
#include <rime/gear/poet.h>
#include <rime/gear/script_translator.h>
#include <rime/gear/translator_commons.h>

// #define DEBUG

bool disable_incremental_search = true;

//static const char* quote_left = "\xef\xbc\x88";
//static const char* quote_right = "\xef\xbc\x89";

namespace rime {

namespace {

struct SyllabifyTask {
  const Code& code;
  const SyllableGraph& graph;
  size_t target_pos;
  function<void (SyllabifyTask* task, size_t depth,
                      size_t current_pos, size_t next_pos)> push;
  function<void (SyllabifyTask* task, size_t depth)> pop;
};

static bool syllabify_dfs(SyllabifyTask* task,
                          size_t depth, size_t current_pos) {
  if (depth == task->code.size()) {
    return current_pos == task->target_pos;
  }
  SyllableId syllable_id = task->code.at(depth);
  auto z = task->graph.edges.find(current_pos);
  if (z == task->graph.edges.end())
    return false;
  // favor longer spellings
  for (const auto& y : boost::adaptors::reverse(z->second)) {
    size_t end_vertex_pos = y.first;
    if (end_vertex_pos > task->target_pos)
      continue;
    auto x = y.second.find(syllable_id);
    if (x != y.second.end()) {
      task->push(task, depth, current_pos, end_vertex_pos);
      if (syllabify_dfs(task, depth + 1, end_vertex_pos))
        return true;
      task->pop(task, depth);
    }
  }
  return false;
}

}  // anonymous namespace

class ScriptSyllabifier : public PhraseSyllabifier {
 public:
  ScriptSyllabifier(ScriptTranslator* translator,
                    Corrector* corrector,
                    const string& input,
                    size_t start)
      : translator_(translator), input_(input), start_(start),
        syllabifier_(translator->delimiters(),
                     translator->enable_completion(),
                     translator->strict_spelling()) {
    if (corrector) {
      syllabifier_.EnableCorrection(corrector);
    }
  }

  virtual Spans Syllabify(const Phrase* phrase);
  size_t BuildSyllableGraph(Prism& prism);
  string GetPreeditString(const Phrase& cand) const;
  string GetOriginalSpelling(const Phrase& cand) const;
  bool IsCandidateCorrection(const Phrase& cand) const;

  const SyllableGraph& syllable_graph() const { return syllable_graph_; }

 protected:
  ScriptTranslator* translator_;
  string input_;
  size_t start_;
  Syllabifier syllabifier_;
  SyllableGraph syllable_graph_;
};

class ScriptTranslation : public Translation {
 public:
  ScriptTranslation(ScriptTranslator* translator,
                    Corrector* corrector,
                    Poet* poet,
                    const string& input,
                    size_t start,
                    SearchContext& search_context)
      : translator_(translator),
        poet_(poet),
        start_(start),
        syllabifier_(New<ScriptSyllabifier>(
            translator, corrector, input, start)),
        enable_correction_(corrector),
        search_context_(search_context),
        input_(input) {
    set_exhausted(true);
  }
  bool Evaluate(Dictionary* dict, UserDictionary* user_dict);
  virtual bool Next();
  virtual an<Candidate> Peek();
 protected:
  bool CheckEmpty();
  bool PreferUserPhrase();
  bool IsNormalSpelling() const;
  void PrepareCandidate();
  template <class QueryResult>
  void EnrollEntries(map<int, DictEntryList>& entries_by_end_pos,
                     const an<QueryResult>& query_result);
  void UpdateSearchState(const string& input);
  an<Sentence> MakeSentence(Dictionary* dict, UserDictionary* user_dict);

  ScriptTranslator* translator_;
  Poet* poet_;
  size_t start_;
  an<ScriptSyllabifier> syllabifier_;
  
  SearchContext& search_context_;
public:
  an<DictEntryCollector> phrase_;
  an<UserDictEntryCollector> user_phrase_;
  an<Sentence> sentence_;
private:
  an<Phrase> candidate_ = nullptr;

  DictEntryCollector::reverse_iterator phrase_iter_;
  UserDictEntryCollector::reverse_iterator user_phrase_iter_;

  size_t max_corrections_ = 4;
  size_t correction_count_ = 0;

  string input_;
  
  bool enable_correction_;
};

// ScriptTranslator implementation

ScriptTranslator::ScriptTranslator(const Ticket& ticket)
    : Translator(ticket),
      Memory(ticket),
      TranslatorOptions(ticket) {
  if (!engine_)
    return;
  if (Config* config = engine_->schema()->config()) {
    config->GetInt(name_space_ + "/spelling_hints", &spelling_hints_);
    config->GetBool(name_space_ + "/always_show_comments",
                    &always_show_comments_);
    config->GetBool(name_space_ + "/enable_correction", &enable_correction_);
    config->GetInt(name_space_ + "/max_homophones", &max_homophones_);
    poet_.reset(new Poet(language(), config));
  }
  if (enable_correction_) {
    if (auto* corrector = Corrector::Require("corrector")) {
      corrector_.reset(corrector->Create(ticket));
    }
  }
}

an<Translation> ScriptTranslator::Query(const string& input,
                                        const Segment& segment) {
  if (!dict_ || !dict_->loaded())
    return nullptr;
  if (!segment.HasTag(tag_))
    return nullptr;
  DLOG(INFO) << "input = '" << input
             << "', [" << segment.start << ", " << segment.end << ")";

  FinishSession();

  bool enable_user_dict = user_dict_ && user_dict_->loaded() &&
      !IsUserDictDisabledFor(input);

  // the translator should survive translations it creates
  auto result = New<ScriptTranslation>(this,
                                       corrector_.get(),
                                       poet_.get(),
                                       input,
                                       segment.start,
                                       search_context_);
  if (!result ||
      !result->Evaluate(dict_.get(),
                        enable_user_dict ? user_dict_.get() : NULL)) {
    return nullptr;
  }

  auto deduped = New<DistinctTranslation>(result);
  if (contextual_suggestions_) {
    return poet_->ContextualWeighted(deduped, input, segment.start, this);
  }
  return deduped;
}

string ScriptTranslator::FormatPreedit(const string& preedit) {
  string result = preedit;
  preedit_formatter_.Apply(&result);
  return result;
}

string ScriptTranslator::Spell(const Code& code) {
  string result;
  vector<string> syllables;
  if (!dict_ || !dict_->Decode(code, &syllables) || syllables.empty())
    return result;
  result =  boost::algorithm::join(syllables,
                                   string(1, delimiters_.at(0)));
  comment_formatter_.Apply(&result);
  return result;
}

string ScriptTranslator::GetPrecedingText(size_t start) const {
  return !contextual_suggestions_ ? string() :
      start > 0 ? engine_->context()->composition().GetTextBefore(start) :
      engine_->context()->commit_history().latest_text();
}

bool ScriptTranslator::Memorize(const CommitEntry& commit_entry) {
  bool update_elements = false;
  // avoid updating single character entries within a phrase which is
  // composed with single characters only
  if (commit_entry.elements.size() > 1) {
    for (const DictEntry* e : commit_entry.elements) {
      if (e->code.size() > 1) {
        update_elements = true;
        break;
      }
    }
  }
  if (update_elements) {
    for (const DictEntry* e : commit_entry.elements) {
      user_dict_->UpdateEntry(*e, 0);
    }
  }
  user_dict_->UpdateEntry(commit_entry, 1);
  return true;
}

// ScriptSyllabifier implementation

Spans ScriptSyllabifier::Syllabify(const Phrase* phrase) {
  Spans result;
  vector<size_t> vertices;
  vertices.push_back(start_);
  SyllabifyTask task{
    phrase->code(),
    syllable_graph_,
    phrase->end() - start_,
    [&](SyllabifyTask* task, size_t depth,
        size_t current_pos, size_t next_pos) {
      vertices.push_back(start_ + next_pos);
    },
    [&](SyllabifyTask* task, size_t depth) {
      vertices.pop_back();
    }
  };
  if (syllabify_dfs(&task, 0, phrase->start() - start_)) {
    result.set_vertices(std::move(vertices));
  }
  return result;
}

size_t ScriptSyllabifier::BuildSyllableGraph(Prism& prism) {
  return (size_t)syllabifier_.BuildSyllableGraph(input_,
                                                 prism,
                                                 &syllable_graph_);
}

bool ScriptSyllabifier::IsCandidateCorrection(const rime::Phrase &cand) const {
  std::stack<bool> results;
  // Perform DFS on syllable graph to find whether this candidate is a correction
  SyllabifyTask task {
    cand.code(),
    syllable_graph_,
    cand.end() - start_,
    [&](SyllabifyTask* task, size_t depth,
        size_t current_pos, size_t next_pos) {
      auto id = cand.code()[depth];
      auto it_s = syllable_graph_.edges.find(current_pos);
      // C++ prohibit operator [] of const map
      // if (syllable_graph_.edges[current_pos][next_pos][id].type == kCorrection)
      if (it_s != syllable_graph_.edges.end()) {
        auto it_e = it_s->second.find(next_pos);
        if (it_e != it_s->second.end()) {
          auto it_type = it_e->second.find(id);
          if (it_type != it_e->second.end()) {
            results.push(it_type->second.is_correction);
            return;
          }
        }
      }
      results.push(false);
    },
    [&](SyllabifyTask* task, size_t depth) {
      results.pop();
    }
  };
  if (syllabify_dfs(&task, 0, cand.start() - start_)) {
    for (; !results.empty(); results.pop()) {
      if (results.top())
        return results.top();
    }
  }
  return false;
}

string ScriptSyllabifier::GetPreeditString(const Phrase& cand) const {
  const auto& delimiters = translator_->delimiters();
  std::stack<size_t> lengths;
  string output;
  SyllabifyTask task{
    cand.code(),
    syllable_graph_,
    cand.end() - start_,
    [&](SyllabifyTask* task, size_t depth,
        size_t current_pos, size_t next_pos) {
      size_t len = output.length();
      if (depth > 0 && len > 0 &&
          delimiters.find(output[len - 1]) == string::npos) {
        output += delimiters.at(0);
      }
      output += input_.substr(current_pos, next_pos - current_pos);
      lengths.push(len);
    },
    [&](SyllabifyTask* task, size_t depth) {
      output.resize(lengths.top());
      lengths.pop();
    }
  };
  if (syllabify_dfs(&task, 0, cand.start() - start_)) {
    return translator_->FormatPreedit(output);
  }
  else {
    return string();
  }
}

string ScriptSyllabifier::GetOriginalSpelling(const Phrase& cand) const {
  if (translator_ &&
      static_cast<int>(cand.code().size()) <= translator_->spelling_hints()) {
    return translator_->Spell(cand.code());
  }
  return string();
}

// ScriptTranslation implementation

static size_t longest_common_prefix(const string& a, const string& b) {
  auto a_begin_it = a.cbegin();
  auto first_mismatch = std::mismatch(a_begin_it, a.cend(), b.cbegin(), b.cend());
  return first_mismatch.first - a_begin_it;
}

void ScriptTranslation::UpdateSearchState(const string& input) {
  size_t& cache_valid_len = search_context_.incremental_search_from_pos;
  cache_valid_len = disable_incremental_search ? 0 : longest_common_prefix(input, search_context_.prev_input);
  
  search_context_.input = input;
  
#ifdef DEBUG
  LOG(ERROR) << "cache_valid_len " << cache_valid_len << " input " << input << " prev_input " << search_context_.prev_input;
#endif
  
  if (cache_valid_len == 0 || disable_incremental_search) {
#ifdef DEBUG
    LOG(ERROR) << "Removing everything";
#endif
    search_context_.prev_words.clear();
    return;
  }
  
  SearchContext::RemoveStateEntriesFromGraph(search_context_.prev_words, cache_valid_len);
  if (!search_context_.prev_words.empty())
    cache_valid_len = search_context_.prev_words.rbegin()->first;
  /*
  cache_valid_len =
  size_t inc_search_from = 0;
  if (!cached_words_it->second.empty()) {
    inc_search_from = cached_words_it->second.rbegin()->first;
  }*/
  /*
  for (auto it = query_result_cache_.begin(); it != query_result_cache_.end();) {
    // Remove entries starting after changed input.
    if (it->first > cache_valid_len) {
#ifdef DEBUG
      // LOG(ERROR) << "Removing row?" << it->second.begin()->second.front()->text;
      LOG(ERROR) << "Removing row " << it->first;
#endif
      it = query_result_cache_.erase(it);
    } else {
      bool remove_row = false;
      auto& entries_same_start_pos = it->second;
      // Remove entry ending after changed input.
      for (auto entry_it = entries_same_start_pos.begin(); entry_it != entries_same_start_pos.end();) {
        const int& end_pos = entry_it->first;
        if (end_pos > cache_valid_len) {
#ifdef DEBUG
          //LOG(ERROR) << "Removing entry?" << entry_it->second.front()->text;
          LOG(ERROR) << "Offending entry endpos=" << end_pos << " cache_valid_len=" << cache_valid_len;
#endif
          // FIX ME remove the whole row instead of just the entry.
          //entry_it = entries_same_start_pos.erase(entry_it);
          remove_row = true;
          break;
        } else {
          entry_it++;
        }
      }
      if (remove_row) {
        #ifdef DEBUG
              // LOG(ERROR) << "Removing row?" << it->second.begin()->second.front()->text;
              LOG(ERROR) << "Removing row due to entry " << it->first;
        #endif
        it = query_result_cache_.erase(it);
      } else {
        it++;
      }
    }
  }*/
}

namespace dictionary {

struct Chunk {
  Table* table = nullptr;
  Code code;
  const table::Entry* entries = nullptr;
  size_t size = 0;
  size_t cursor = 0;
  string remaining_code;  // for predictive queries
  double credibility = 0.0;

  Chunk() = default;
  Chunk(Table* t, const Code& c, const table::Entry* e, double cr = 0.0)
      : table(t), code(c), entries(e), size(1), cursor(0), credibility(cr) {}
  Chunk(Table* t, const TableAccessor& a, double cr = 0.0)
      : Chunk(t, a, string(), cr) {}
  Chunk(Table* t, const TableAccessor& a, const string& r, double cr = 0.0)
      : table(t), code(a.index_code()), entries(a.entry()),
        size(a.remaining()), cursor(0), remaining_code(r), credibility(cr) {}
};

struct QueryResult {
  vector<Chunk> chunks;
};
}

bool ScriptTranslation::Evaluate(Dictionary* dict, UserDictionary* user_dict) {
  size_t consumed = syllabifier_->BuildSyllableGraph(*dict->prism());
  const SyllableGraph& syllable_graph = syllabifier_->syllable_graph();
  /*
#ifdef DEBUG
  if (!search_context_.prev_input.empty()) {
    LOG(ERROR) << "Query: " << input_ << " prev query: " << search_context_.prev_input;
  } else {
    LOG(ERROR) << "New query: " << input_;
  }
#endif*/
  /*
  if (!query_result_cache_.empty()) {
    LOG(ERROR) << "FIRST ROW SIZE: " << query_result_cache_.begin()->second.size();
  } else {
    LOG(ERROR) << "EMPTY WORD GRAPH";
  }*/
  
  //UpdateSearchState(input_);
  // if (prev_phrase_ && !prev_phrase_->empty()) incremental_search_from = prev_phrase_->rbegin()->first;
  // phrase_ = dict->LookupIncremental(syllable_graph, 0, incremental_search_from, 0);
  
  LOG(ERROR) << "Pharse";
  phrase_ = dict->Lookup(syllable_graph, 0);
  
  /*
  if (!disable_incremental_search) {
    if (phrase_ && prev_phrase_) {
      for (auto it = prev_phrase_->begin(); it != prev_phrase_->end(); ++it) {
        auto& entries_at_end_pos = it->second;
        entries_at_end_pos.Reset();
        (*phrase_)[it->first] = entries_at_end_pos;
      }
    }
  }*/
  
  /*
  LOG(ERROR) << "query.size=" << phrase_->size();
  for (auto it = phrase_->begin(); it != phrase_->end(); ++it) {
    auto& query_result_ = it->second.query_result_;
    if (query_result_) {
      LOG(ERROR) << "query[" << it->first << "].size=" << it->second.query_result_->chunks.size();
    } else {
      LOG(ERROR) << "query[" << it->first << "].size=" << 0;
    }
  }*/
  
  // LOG(ERROR) << "Copying " << it->first << " " <<
  
  if (user_dict) {
    user_phrase_ = user_dict->Lookup(syllable_graph, 0);
  }
  if (!phrase_ && !user_phrase_)
    return false;
  // make sentences when there is no exact-matching phrase candidate
  size_t translated_len = 0;
  if (phrase_ && !phrase_->empty())
    translated_len = (std::max)(translated_len, phrase_->rbegin()->first);
  if (user_phrase_ && !user_phrase_->empty())
    translated_len = (std::max)(translated_len, user_phrase_->rbegin()->first);

  if (phrase_)
    phrase_iter_ = phrase_->rbegin();
  if (user_phrase_)
    user_phrase_iter_ = user_phrase_->rbegin();

  // If the first candidate is a correction, make sentense.
  bool is_first_candidate_a_correction = false;
  if (enable_correction_) {
      CheckEmpty();
      PrepareCandidate();
      if (candidate_) {
          is_first_candidate_a_correction = syllabifier_->IsCandidateCorrection(*candidate_);
      }
  }

  if ((translated_len < consumed || is_first_candidate_a_correction) &&
      syllable_graph.edges.size() > 1) {  // at least 2 syllables required
    sentence_ = MakeSentence(dict, user_dict);
  }

  return !CheckEmpty();
}

bool ScriptTranslation::Next() {
  bool is_correction;
  do {
    is_correction = false;
    if (exhausted())
      return false;
    if (sentence_) {
      sentence_.reset();
      return !CheckEmpty();
    }
    int phrase_code_length = 0;
    if (phrase_ && phrase_iter_ != phrase_->rend()) {
      phrase_code_length = phrase_iter_->first;
    }
    if (PreferUserPhrase()) {
      UserDictEntryIterator& uter(user_phrase_iter_->second);
      if (!uter.Next()) {
        ++user_phrase_iter_;
      }
    }
    else if (phrase_code_length > 0) {
      DictEntryIterator& iter(phrase_iter_->second);
      if (!iter.Next()) {
        ++phrase_iter_;
      }
    }
    if (enable_correction_) {
      PrepareCandidate();
      if (!candidate_) {
        break;
      }
      is_correction = syllabifier_->IsCandidateCorrection(*candidate_);
    }
  } while ( // limit the number of correction candidates
      enable_correction_ &&
      is_correction &&
      correction_count_ > max_corrections_);
  if (is_correction) {
    ++correction_count_;
  }
  return !CheckEmpty();
}

bool ScriptTranslation::IsNormalSpelling() const {
  const auto& syllable_graph = syllabifier_->syllable_graph();
  return !syllable_graph.vertices.empty() &&
      (syllable_graph.vertices.rbegin()->second == kNormalSpelling);
}

an<Candidate> ScriptTranslation::Peek() {
  PrepareCandidate();
  if (!candidate_) {
    return nullptr;
  }
  if (candidate_->preedit().empty()) {
    candidate_->set_preedit(syllabifier_->GetPreeditString(*candidate_));
  }
  if (candidate_->comment().empty()) {
    auto spelling = syllabifier_->GetOriginalSpelling(*candidate_);
    if (!spelling.empty() &&
        (translator_->always_show_comments() ||
          spelling != candidate_->preedit())) {
      candidate_->set_comment(/*quote_left + */spelling/* + quote_right*/);
    }
  }
  candidate_->set_syllabifier(syllabifier_);
  return candidate_;
}

bool ScriptTranslation::PreferUserPhrase() {
  int user_phrase_code_length = 0;
  double user_phrase_weight = 0;
  if (user_phrase_ && user_phrase_iter_ != user_phrase_->rend()) {
    user_phrase_code_length = user_phrase_iter_->first;
    UserDictEntryIterator& uter = user_phrase_iter_->second;
    const auto& entry = uter.Peek();
    user_phrase_weight = entry->weight;
  }

  int phrase_code_length = 0;
  double phrase_weight = std::numeric_limits<double>::lowest();
  if (phrase_ && phrase_iter_ != phrase_->rend()) {
    phrase_code_length = phrase_iter_->first;
    DictEntryIterator& iter = phrase_iter_->second;
    const auto& entry = iter.Peek();
    phrase_weight = entry->weight;
  }

  return user_phrase_code_length > 0 &&
         user_phrase_code_length >= phrase_code_length &&
         (user_phrase_code_length > phrase_code_length || user_phrase_weight >= phrase_weight);
}

void ScriptTranslation::PrepareCandidate() {
  if (exhausted()) {
    candidate_ = nullptr;
    return;
  }
  if (sentence_) {
    candidate_ = sentence_;
    return;
  }
  size_t phrase_code_length = 0;
  if (phrase_ && phrase_iter_ != phrase_->rend()) {
    phrase_code_length = phrase_iter_->first;
  }
  an<Phrase> cand;
  if (PreferUserPhrase()) {
    size_t user_phrase_code_length = user_phrase_iter_->first;

    UserDictEntryIterator& uter = user_phrase_iter_->second;
    const auto& entry = uter.Peek();
    DLOG(INFO) << "user phrase '" << entry->text
               << "', code length: " << user_phrase_code_length;
    cand = New<Phrase>(translator_->language(),
                       "user_phrase",
                       start_,
                       start_ + user_phrase_code_length,
                       entry);
    cand->set_quality(exp(entry->weight) +
                      translator_->initial_quality() +
                      (IsNormalSpelling() ? 0.5 : -0.5));
  }
  else if (phrase_code_length > 0) {
    DictEntryIterator& iter = phrase_iter_->second;
    const auto& entry = iter.Peek();
    DLOG(INFO) << "phrase '" << entry->text
               << "', code length: " << phrase_code_length;
    cand = New<Phrase>(translator_->language(),
                       "phrase",
                       start_,
                       start_ + phrase_code_length,
                       entry);
    cand->set_quality(exp(entry->weight) +
                      translator_->initial_quality() +
                      (IsNormalSpelling() ? 0 : -1));
  }
  candidate_ = cand;
}

bool ScriptTranslation::CheckEmpty() {
  set_exhausted((!phrase_ || phrase_iter_ == phrase_->rend()) &&
                (!user_phrase_ || user_phrase_iter_ == user_phrase_->rend()));
  return exhausted();
}

template <class QueryResult>
void ScriptTranslation::EnrollEntries(
    map<int, DictEntryList>& entries_by_end_pos,
    const an<QueryResult>& query_result) {
  if (query_result) {
    for (auto& y : *query_result) {
      DictEntryList& homophones = entries_by_end_pos[y.first];
      while (homophones.size() < translator_->max_homophones() &&
             !y.second.exhausted()) {
        homophones.push_back(y.second.Peek());
        // LOG(ERROR) << "FUCK " << y.first << " " << y.second.Peek()->text;
        if (!y.second.Next())
          break;
      }
    }
  }
}

an<Sentence> ScriptTranslation::MakeSentence(Dictionary* dict,
                                             UserDictionary* user_dict) {
  const int kMaxSyllablesForUserPhraseQuery = 5;
  const auto& syllable_graph = syllabifier_->syllable_graph();
  
  LOG(ERROR) << "MakeSentence new input: " << input_ << " prev " << search_context_.prev_input;
  UpdateSearchState(input_);
  
  WordGraph& graph = search_context_.prev_words;
  for (const auto& x : syllable_graph.edges) {
    auto cached_words_it = graph.find(x.first);
    bool full_search = cached_words_it == graph.end();
    auto& same_start_pos = graph[x.first];
    if (user_dict) {
      EnrollEntries(same_start_pos,
                    user_dict->Lookup(syllable_graph,
                                      x.first,
                                      kMaxSyllablesForUserPhraseQuery));
    }
    // merge lookup results
    EnrollEntries(same_start_pos, dict->LookupIncremental(syllable_graph, x.first, &search_context_, 0));
  }
  
  for (auto it = graph.begin(); it != graph.end(); it++) {
    auto words = it->second;
    string log;
    for (auto words_it = words.begin(); words_it != words.end(); words_it++) {
      auto dict = words_it->second;
      for (auto word_it = dict.begin(); word_it != dict.end(); word_it++) {
        log += (*word_it)->text + " ";
      }
    }
    LOG(ERROR) << "Word cloud start: " << it->first << " " << log;
  }
  search_context_.prev_input = input_;
  if (auto sentence =
      poet_->MakeSentence(graph,
                          syllable_graph.interpreted_length,
                          translator_->GetPrecedingText(start_))) {
    sentence->Offset(start_);
    sentence->set_syllabifier(syllabifier_);
    return sentence;
  }
  return nullptr;
}

}  // namespace rime
