//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-07-02 GONG Chen <chen.sst@gmail.com>
//
#include <cfloat>
#include <cstring>
#include <algorithm>
#include <queue>
#include <utility>
#include <rime/common.h>
#include <rime/algo/syllabifier.h>
#include <rime/dict/table.h>
#include <boost/range/adaptor/reversed.hpp>

namespace std {
  size_t hash<rime::IndexCode>::operator()(const rime::IndexCode& code) const {
    return boost::hash_range(code.cbegin(), code.cbegin() + code.size());
  }
}

namespace rime {

const char kTableFormatLatest[] = "Rime::Table/4.0";
const int kTableFormatLowestCompatible = 4.0;

const char kTableFormatPrefix[] = "Rime::Table/";
const size_t kTableFormatPrefixLen = sizeof(kTableFormatPrefix) - 1;

void IndexCode::clear() {
  size_ = 0;
  memset(this, 0, max_size() * sizeof(SyllableId));
}

SyllableId IndexCode::pop_back() {
  assert(size_ > 0);
  return (*this)[size_--];
}

void IndexCode::push_back(SyllableId syllable_id) {
  assert(size_ < max_size());
  return (*this)[size_++] = syllable_id;
}

size_t IndexCode::size() const {
  return size_;
}

class TableQuery {
 public:
  TableQuery(table::Index* index) : lv1_index_(index) {
    Reset();
  }

  TableAccessor Access(SyllableId syllable_id,
                       double credibility = 0.0) const;

  // down to next level
  bool Advance(SyllableId syllable_id, double credibility = 0.0);

  // up one level
  bool Backdate();

  // back to root
  void Reset();

  size_t level() const { return level_; }

 protected:
  size_t level_ = 0;
public:
  IndexCode index_code_;
protected:
  vector<double> credibility_;

 private:
  bool Walk(SyllableId syllable_id);

  table::HeadIndex* lv1_index_ = nullptr;
  table::TrunkIndex* lv2_index_ = nullptr;
  table::TrunkIndex* lv3_index_ = nullptr;
  table::TailIndex* lv4_index_ = nullptr;
};

TableAccessor::TableAccessor(const IndexCode& index_code,
                             const List<table::Entry>* list,
                             double credibility)
    : index_code_(index_code),
      entries_(list->at.get()),
      size_(list->size),
      credibility_(credibility) {
}

TableAccessor::TableAccessor(const IndexCode& index_code,
                             const Array<table::Entry>* array,
                             double credibility)
    : index_code_(index_code),
      entries_(array->at),
      size_(array->size),
      credibility_(credibility) {
}

TableAccessor::TableAccessor(const IndexCode& index_code,
                             const table::TailIndex* code_map,
                             double credibility)
    : index_code_(index_code),
      long_entries_(code_map->at),
      size_(code_map->size),
      credibility_(credibility) {
}

bool TableAccessor::exhausted() const {
  if (entries_ || long_entries_) {
    return !(size_ - cursor_);
  }
  return true;
}

size_t TableAccessor::remaining() const {
  if (entries_ || long_entries_) {
    return size_ - cursor_;
  }
  return 0;
}

const table::Entry* TableAccessor::entry() const {
  if (exhausted())
    return NULL;
  if (entries_)
    return &entries_[cursor_];
  else
    return &long_entries_[cursor_].entry;
}

const table::Code* TableAccessor::extra_code() const {
  if (!long_entries_ || cursor_ >= size_)
    return NULL;
  return &long_entries_[cursor_].extra_code;
}

Code TableAccessor::code() const {
  auto extra = extra_code();
  Code code(index_code());
  if (!extra) {
    return code;
  }
  for (auto p = extra->begin(); p != extra->end(); ++p) {
    code.push_back(*p);
  }
  return code;
}

bool TableAccessor::Next() {
  if (exhausted())
    return false;
  ++cursor_;
  return !exhausted();
}

bool TableQuery::Advance(SyllableId syllable_id, double credibility) {
  if (!Walk(syllable_id)) {
    return false;
  }
  ++level_;
  index_code_.push_back(syllable_id);
  credibility_.push_back(credibility_.back() + credibility);
  return true;
}

bool TableQuery::Backdate() {
  if (level_ == 0)
    return false;
  --level_;
  if (index_code_.size() > level_) {
    index_code_.pop_back();
    credibility_.pop_back();
  }
  return true;
}

void TableQuery::Reset() {
  level_ = 0;
  index_code_.clear();
  credibility_.clear();
  credibility_.push_back(0.0);
}

inline static bool node_less(const table::TrunkIndexNode& a,
                             const table::TrunkIndexNode& b) {
  return a.key < b.key;
}

static table::TrunkIndexNode* find_node(table::TrunkIndexNode* first,
                                        table::TrunkIndexNode* last,
                                        const SyllableId& key) {
  table::TrunkIndexNode target;
  target.key = key;
  auto it = std::lower_bound(first, last, target, node_less);
  return it == last || key < it->key ? last : it;
}

bool TableQuery::Walk(SyllableId syllable_id) {
  if (level_ == 0) {
    if (!lv1_index_ ||
        syllable_id < 0 ||
        syllable_id >= static_cast<SyllableId>(lv1_index_->size))
      return false;
    auto node = &lv1_index_->at[syllable_id];
    if (!node->next_level)
      return false;
    lv2_index_ = &node->next_level->trunk();
  }
  else if (level_ == 1) {
    if (!lv2_index_)
      return false;
    auto node = find_node(lv2_index_->begin(), lv2_index_->end(), syllable_id);
    if (node == lv2_index_->end())
      return false;
    if (!node->next_level)
      return false;
    lv3_index_ = &node->next_level->trunk();
  }
  else if (level_ == 2) {
    if (!lv3_index_)
      return false;
    auto node = find_node(lv3_index_->begin(), lv3_index_->end(), syllable_id);
    if (node == lv3_index_->end())
      return false;
    if (!node->next_level)
      return false;
    lv4_index_ = &node->next_level->tail();
  }
  else {
    return false;
  }
  return true;
}

inline static IndexCode add_syllable(IndexCode code, SyllableId syllable_id) {
  code.push_back(syllable_id);
  return code;
}

TableAccessor TableQuery::Access(SyllableId syllable_id,
                                 double credibility) const {
  credibility += credibility_.back();
  if (level_ == 0) {
    if (!lv1_index_ ||
        syllable_id < 0 ||
        syllable_id >= static_cast<SyllableId>(lv1_index_->size))
      return TableAccessor();
    auto node = &lv1_index_->at[syllable_id];
    return TableAccessor(add_syllable(index_code_, syllable_id),
                         &node->entries, credibility);
  }
  else if (level_ == 1 || level_ == 2) {
    auto index = (level_ == 1) ? lv2_index_ : lv3_index_;
    if (!index)
      return TableAccessor();
    auto node = find_node(index->begin(), index->end(), syllable_id);
    if (node == index->end())
      return TableAccessor();
    return TableAccessor(add_syllable(index_code_, syllable_id),
                         &node->entries, credibility);
  }
  else if (level_ == 3) {
    if (!lv4_index_)
      return TableAccessor();
    return TableAccessor(index_code_, lv4_index_, credibility);
  }
  return TableAccessor();
}

// string Table::GetString_v1(const table::StringType& x) {
//  return x.str().c_str();
// }

// bool Table::AddString_v1(const string& src, table::StringType* dest,
//                          double /*weight*/) {
//   return CopyString(src, &dest->str());
// }

string Table::GetString(const table::StringType& x) {
  return string_table_->GetString(x.str_id());
}

bool Table::AddString(const string& src, table::StringType* dest,
                      double weight) {
  string_table_builder_->Add(src, weight, &dest->str_id());
  return true;
}

bool Table::OnBuildStart() {
  string_table_builder_.reset(new StringTableBuilder);
  return true;
}

bool Table::OnBuildFinish() {
  string_table_builder_->Build();
  // saving string table image
  size_t image_size = string_table_builder_->BinarySize();
  char* image = Allocate<char>(image_size);
  if (!image) {
    LOG(ERROR) << "Error creating string table image.";
    return false;
  }
  string_table_builder_->Dump(image, image_size);
  metadata_->string_table = image;
  metadata_->string_table_size = image_size;
  return true;
}

bool Table::OnLoad() {
  string_table_.reset(new StringTable(metadata_->string_table.get(),
                                      metadata_->string_table_size));
  return true;
}

Table::Table(const string& file_name) : MappedFile(file_name) {
}

Table::~Table() {
}

bool Table::Load() {
  LOG(INFO) << "loading table file: " << file_name();

  if (IsOpen())
    Close();

  if (!OpenReadOnly()) {
    LOG(ERROR) << "Error opening table file '" << file_name() << "'.";
    return false;
  }

  metadata_ = Find<table::Metadata>(0);
  if (!metadata_) {
    LOG(ERROR) << "metadata not found.";
    Close();
    return false;
  }
  if (strncmp(metadata_->format, kTableFormatPrefix, kTableFormatPrefixLen)) {
    LOG(ERROR) << "invalid metadata.";
    Close();
    return false;
  }
  double format_version = atof(&metadata_->format[kTableFormatPrefixLen]);
  if (format_version < kTableFormatLowestCompatible - DBL_EPSILON) {
    LOG(ERROR) << "table format version " << format_version
               << " is no longer supported. please upgrade to version "
               << kTableFormatLatest;
    return false;
  }

  syllabary_ = metadata_->syllabary.get();
  if (!syllabary_) {
    LOG(ERROR) << "syllabary not found.";
    Close();
    return false;
  }
  index_ = metadata_->index.get();
  if (!index_) {
    LOG(ERROR) << "table index not found.";
    Close();
    return false;
  }

  return OnLoad();
}

bool Table::Save() {
  LOG(INFO) << "saving table file: " << file_name();

  if (!index_) {
    LOG(ERROR) << "the table has not been constructed!";
    return false;
  }

  return ShrinkToFit();
}

uint32_t Table::dict_file_checksum() const {
  return metadata_ ? metadata_->dict_file_checksum : 0;
}

bool Table::Build(const Syllabary& syllabary, const Vocabulary& vocabulary,
                  size_t num_entries, uint32_t dict_file_checksum) {
  const size_t kReservedSize = 4096;
  size_t num_syllables = syllabary.size();
  size_t estimated_file_size = kReservedSize + 32 * num_syllables + 64 * num_entries;
  LOG(INFO) << "building table.";
  LOG(INFO) << "num syllables: " << num_syllables;
  LOG(INFO) << "num entries: " << num_entries;
  LOG(INFO) << "estimated file size: " << estimated_file_size;
  if (!Create(estimated_file_size)) {
    LOG(ERROR) << "Error creating table file '" << file_name() << "'.";
    return false;
  }

  LOG(INFO) << "creating metadata.";
  metadata_ = Allocate<table::Metadata>();
  if (!metadata_) {
    LOG(ERROR) << "Error creating metadata in file '" << file_name() << "'.";
    return false;
  }
  metadata_->dict_file_checksum = dict_file_checksum;
  metadata_->num_syllables = num_syllables;
  metadata_->num_entries = num_entries;

  if (!OnBuildStart()) {
    return false;
  }

  LOG(INFO) << "creating syllabary.";
  syllabary_ = CreateArray<table::StringType>(num_syllables);
  if (!syllabary_) {
    LOG(ERROR) << "Error creating syllabary.";
    return false;
  }
  else {
    size_t i = 0;
    for (const string& syllable : syllabary) {
      AddString(syllable, &syllabary_->at[i++], 0.0);
    }
  }
  metadata_->syllabary = syllabary_;

  LOG(INFO) << "creating table index.";
  index_ = BuildIndex(vocabulary, num_syllables);
  if (!index_) {
    LOG(ERROR) << "Error creating table index.";
    return false;
  }
  metadata_->index = index_;

  if (!OnBuildFinish()) {
    return false;
  }

  // at last, complete the metadata
  std::strncpy(metadata_->format, kTableFormatLatest,
               table::Metadata::kFormatMaxLength);
  return true;
}

table::Index* Table::BuildIndex(const Vocabulary& vocabulary,
                                size_t num_syllables) {
  return reinterpret_cast<table::Index*>(BuildHeadIndex(vocabulary,
                                                        num_syllables));
}

table::HeadIndex* Table::BuildHeadIndex(const Vocabulary& vocabulary,
                                        size_t num_syllables) {
  auto index = CreateArray<table::HeadIndexNode>(num_syllables);
  if (!index) {
    return NULL;
  }
  for (const auto& v : vocabulary) {
    int syllable_id = v.first;
    auto& node(index->at[syllable_id]);
    const auto& entries(v.second.entries);
    if (!BuildEntryList(entries, &node.entries)) {
        return NULL;
    }
    if (v.second.next_level) {
      Code code;
      code.push_back(syllable_id);
      auto next_level_index = BuildTrunkIndex(code, *v.second.next_level);
      if (!next_level_index) {
        return NULL;
      }
      node.next_level = reinterpret_cast<table::PhraseIndex*>(next_level_index);
    }
  }
  return index;
}

table::TrunkIndex* Table::BuildTrunkIndex(const Code& prefix,
                                          const Vocabulary& vocabulary) {
  auto index = CreateArray<table::TrunkIndexNode>(vocabulary.size());
  if (!index) {
    return NULL;
  }
  size_t count = 0;
  for (const auto& v : vocabulary) {
    int syllable_id = v.first;
    auto& node(index->at[count++]);
    node.key = syllable_id;
    const auto& entries(v.second.entries);
    if (!BuildEntryList(entries, &node.entries)) {
        return NULL;
    }
    if (v.second.next_level) {
      Code code(prefix);
      code.push_back(syllable_id);
      if (code.size() < Code::kIndexCodeMaxLength) {
        auto next_level_index = BuildTrunkIndex(code, *v.second.next_level);
        if (!next_level_index) {
          return NULL;
        }
        node.next_level =
            reinterpret_cast<table::PhraseIndex*>(next_level_index);
      }
      else {
        auto tail_index = BuildTailIndex(code, *v.second.next_level);
        if (!tail_index) {
          return NULL;
        }
        node.next_level = reinterpret_cast<table::PhraseIndex*>(tail_index);
      }
    }
  }
  return index;
}

table::TailIndex* Table::BuildTailIndex(const Code& prefix,
                                        const Vocabulary& vocabulary) {
  if (vocabulary.find(-1) == vocabulary.end()) {
    return NULL;
  }
  const auto& page(vocabulary.find(-1)->second);
  DLOG(INFO) << "page size: " << page.entries.size();
  auto index = CreateArray<table::LongEntry>(page.entries.size());
  if (!index) {
    return NULL;
  }
  size_t count = 0;
  for (const auto& src : page.entries) {
    DLOG(INFO) << "count: " << count;
    DLOG(INFO) << "entry: " << src->text;
    auto& dest(index->at[count++]);
    size_t extra_code_length = src->code.size() - Code::kIndexCodeMaxLength;
    DLOG(INFO) << "extra code length: " << extra_code_length;
    dest.extra_code.size = extra_code_length;
    dest.extra_code.at = Allocate<SyllableId>(extra_code_length);
    if (!dest.extra_code.at) {
      LOG(ERROR) << "Error creating code sequence; file size: " << file_size();
      return NULL;
    }
    std::copy(src->code.begin() + Code::kIndexCodeMaxLength,
              src->code.end(),
              dest.extra_code.begin());
    BuildEntry(*src, &dest.entry);
  }
  return index;
}

Array<table::Entry>* Table::BuildEntryArray(const DictEntryList& entries) {
  auto array = CreateArray<table::Entry>(entries.size());
  if (!array) {
    return NULL;
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    if (!BuildEntry(*entries[i], &array->at[i])) {
      return NULL;
    }
  }
  return array;
}

bool Table::BuildEntryList(const DictEntryList& src,
                           List<table::Entry>* dest) {
  if (!dest)
    return false;
  dest->size = src.size();
  dest->at = Allocate<table::Entry>(src.size());
  if (!dest->at) {
    LOG(ERROR) << "Error creating table entries; file size: " << file_size();
    return false;
  }
  size_t i = 0;
  for (auto d = src.begin(); d != src.end(); ++d, ++i) {
    if (!BuildEntry(**d, &dest->at[i]))
      return false;
  }
  return true;
}

bool Table::BuildEntry(const DictEntry& dict_entry, table::Entry* entry) {
  if (!entry)
    return false;
  if (!AddString(dict_entry.text, &entry->text, dict_entry.weight)) {
    LOG(ERROR) << "Error creating table entry '" << dict_entry.text
               << "'; file size: " << file_size();
    return false;
  }
  entry->weight = static_cast<table::Weight>(dict_entry.weight);
  return true;
}

bool Table::GetSyllabary(Syllabary* result) {
  if (!result || !syllabary_)
    return false;
  for (size_t i = 0; i < syllabary_->size; ++i) {
    result->insert(GetSyllableById(static_cast<SyllableId>(i)));
  }
  return true;
}
string Table::GetSyllableById(SyllableId syllable_id) {
  if (!syllabary_ ||
      syllable_id < 0 ||
      syllable_id >= static_cast<SyllableId>(syllabary_->size))
    return string();
  return GetString(syllabary_->at[syllable_id]);
}

TableAccessor Table::QueryWords(SyllableId syllable_id) {
  TableQuery query(index_);
  return query.Access(syllable_id);
}

TableAccessor Table::QueryPhrases(const Code& code) {
  if (code.empty())
    return TableAccessor();
  TableQuery query(index_);
  for (size_t i = 0; i < Code::kIndexCodeMaxLength; ++i) {
    if (code.size() == i + 1)
      return query.Access(code[i]);
    if (!query.Advance(code[i]))
      return TableAccessor();
  }
  return query.Access(-1);
}

struct QueryNode {
  size_t pos;
  TableQuery table_query;
  
  QueryNode(const size_t& pos, const TableQuery& table_query) : pos(pos), table_query(table_query) {
  }
};

// using QueryNode = pair<size_t, TableQuery>;

bool Table::Query(const SyllableGraph& syll_graph,
                  size_t start_pos,
                  TableQueryResult* result) {
  return Query(syll_graph, nullptr, start_pos, result);
}

static string index_code_to_string(Table* table, const IndexCode& index_code) {
  string result;
  for (auto it = index_code.begin(); it != index_code.end(); ++it) {
    result += table->GetSyllableById(*it) + " ";
  }
  return result;
}

struct EdgeDfsNode {
  size_t cur_pos = 0;
  IndexCode path;
  bool has_added_first_new_node_in_path = false;
  size_t table_accessor_pos = cur_pos;
};

static void transpose_graph(size_t interpreted_length, const EdgeMap& edges, SpellingIndices& indices) {
  indices.clear();
  indices.resize(interpreted_length);
  for (const auto& start : edges) {
    auto& index(indices[start.first]);
    for (const auto& end : boost::adaptors::reverse(start.second)) {
      for (const auto& spelling : end.second) {
        SyllableId syll_id = spelling.first;
        index[syll_id].push_back(&spelling.second);
      }
    }
  }
}

static size_t longest_common_prefix(const string& a, const string& b) {
  auto a_begin_it = a.cbegin();
  auto first_mismatch = std::mismatch(a_begin_it, a.cend(), b.cbegin(), b.cend());
  return first_mismatch.first - a_begin_it;
}

bool Table::Query(const SyllableGraph& syll_graph,
                  an<SyllableGraph> prev_graph,
                  size_t start_pos,
                  TableQueryResult* result) {
  if (!result ||
      !index_ ||
      start_pos >= syll_graph.interpreted_length)
    return false;
  
  result->clear();
  std::queue<QueryNode> q;
  
  size_t max_reusable_index = 0;
  if (prev_graph)
    longest_common_prefix(prev_graph->input, syll_graph.input);
  
  bool use_new_graph_indices = false;
  SpellingIndices new_indices;
  
  if (//false &&
      prev_graph) {
    LOG(ERROR) << "UFO Query: " << syll_graph.input.substr(start_pos) << " Prev query: " << prev_graph->input;
    
    const EdgeMap& prev_edges = prev_graph->edges;
    const EdgeMap& cur_edges = syll_graph.edges;
    EdgeMap new_edges;
    
    std::queue<EdgeDfsNode> dfs;
    dfs.push({start_pos, IndexCode(), false});
    
    while (!dfs.empty()) {
      const auto dfs_node = std::move(dfs.front());
      dfs.pop();
      LOG(ERROR) << " UFO DFS " << index_code_to_string(this, dfs_node.path);
      
      const size_t& cur_pos = dfs_node.cur_pos;
      const auto cur_outgoing_vertices_it = cur_edges.find(cur_pos);
      
      if (cur_outgoing_vertices_it == cur_edges.end()) continue;
      const EndVertexMap& cur_outgoing_vertices = cur_outgoing_vertices_it->second;
      
      const auto prev_outgoing_vertices_it = prev_edges.find(cur_pos);
      bool no_prev_outgoing_vertics = prev_outgoing_vertices_it == prev_edges.end();
      
      bool should_add_initial_vertex = false;
      for (const auto& cur_outgoing_vertex : cur_outgoing_vertices) {
        const size_t& end_pos = cur_outgoing_vertex.first;
        const SpellingMap& spellings = cur_outgoing_vertex.second;
        
        /*
        bool no_prev_outgoing_vertex = no_prev_outgoing_vertics;
        if (!no_prev_outgoing_vertex) {
          // If there are prev outgoing vertices, make sure there are edges ending at the same end_pos.
          const EndVertexMap& prev_outgoing_vertices = prev_outgoing_vertices_it->second;
          const auto prev_outgoing_vertex_it = prev_outgoing_vertices.find(end_pos);
          no_prev_outgoing_vertex = no_prev_outgoing_vertex || prev_outgoing_vertex_it == prev_outgoing_vertices.end();
        }*/
        
        for (const auto& spelling : spellings) {
          const SyllableId& syllable_id = spelling.first;
          
          bool is_new_edge = end_pos > max_reusable_index;
          /*if (no_prev_outgoing_vertex) {
            is_new_edge = true;
          } else {
            const EndVertexMap& prev_outgoing_vertices = prev_outgoing_vertices_it->second;
            const auto prev_outgoing_vertex_it = prev_outgoing_vertices.find(end_pos);
            const SpellingMap& prev_spellings = prev_outgoing_vertex_it->second;
            
            is_new_edge = prev_spellings.find(syllable_id) == prev_spellings.end();
          }*/
          
          {
            IndexCode new_path(dfs_node.path);
            bool is_below_level_4 = dfs_node.path.size() < Code::kIndexCodeMaxLength;
            if (is_below_level_4)
              new_path.push_back(syllable_id);
            dfs.push({end_pos, new_path, is_new_edge, is_below_level_4 ? end_pos : cur_pos});
          }
          
          if (is_new_edge) {
            if (!dfs_node.has_added_first_new_node_in_path)
              should_add_initial_vertex = true;
            // Add this new edge to the new edges map.
            new_edges[cur_pos][end_pos][syllable_id] = spelling.second;
            /*if (syll_graph.input == prev_graph->input) {
              LOG(ERROR) << "[BUG] new edge(" << cur_pos << "," << end_pos << ")=" << index_code_to_string(this, new_path);
            } else*/
            
            bool is_terminal_index_code = dfs_node.path.size() == Code::kIndexCodeMaxLength;

            if (!is_terminal_index_code) {
              LOG(ERROR) << "  UFO new syllable_id=" << syllable_id << " new edge(" << cur_pos << "," << end_pos << ")=" << index_code_to_string(this, dfs_node.path) << " " << GetSyllableById(syllable_id);
            } else {
              LOG(ERROR) << "  UFO new syllable_id=" << syllable_id << " T new edge(" << cur_pos << "," << end_pos << ")=" << index_code_to_string(this, dfs_node.path) << " " << GetSyllableById(syllable_id);
            }
          }
        }
      }
      
      if (should_add_initial_vertex) {
        //LOG(ERROR) << "Add path size: " << new_path.size();
        const IndexCode& path = dfs_node.path;
        if (path.size() <= 3) {
          // Add the current node to the initial search set to search new edges.
          TableQuery initial_state(index_);
          /*
          if (path.size() >= 2 && path[0] == 131 && path[1] == 381) {
            LOG(ERROR) << " Add node " << initial_state.index_code_.size();
          }*/
          
          bool has_such_entry = true;
          for (const auto& syllable_id : path) {
            //LOG(ERROR) << " Add node " << syllable_id;
            has_such_entry = initial_state.Advance(syllable_id);
            if (!has_such_entry) break;
          }
          
          if (!has_such_entry) continue;
          
          LOG(ERROR) << " Add new initial node: " << cur_pos << " path: " <<  index_code_to_string(this, path) << " phyiscal path " << index_code_to_string(this, initial_state.index_code_);
          q.push({dfs_node.table_accessor_pos, initial_state});
        }
      }
    }
    use_new_graph_indices = true;
    transpose_graph(syll_graph.interpreted_length, syll_graph.edges, new_indices);
    /*
    if (q.empty()) {
      TableQuery initial_state(index_);
      q.push({start_pos, initial_state});
    }*/
  } else {
    LOG(ERROR) << "Query: " << syll_graph.input.substr(start_pos);
    
    result->clear();
    
    TableQuery initial_state(index_);
    q.push({start_pos, initial_state});
  }
  result->clear();
  
  const SpellingIndices& indices = use_new_graph_indices ? new_indices : syll_graph.indices;
  
  while (!q.empty()) {
    size_t current_pos = q.front().pos;
    TableQuery query(q.front().table_query);
    q.pop();
    if (current_pos >= indices.size()) {
      continue;
    }
    LOG(ERROR) << "DFS: " <<  current_pos << " " << index_code_to_string(this, query.index_code_);
    if (index_code_to_string(this, query.index_code_) == "diu nei lou " && current_pos == 12) {
      LOG(ERROR) << " debug";
    }
    auto& index = indices[current_pos];
    if (query.level() == Code::kIndexCodeMaxLength) {
      TableAccessor accessor(query.Access(-1));
      if (!accessor.exhausted()) {
        TableAccessor log_accessor(accessor);
        string log;
        while (!log_accessor.exhausted()) {
          log += GetEntryText(*(log_accessor.entry())) + " ";
          log_accessor.Next();
        }
        LOG(ERROR) << "  DFS add entry " << log << " " << current_pos;
        (*result)[current_pos].push_back(accessor);
      }
      continue;
    }
    for (const auto& spellings : index) {
      SyllableId syll_id = spellings.first;
      for (auto props : spellings.second) {
        TableAccessor accessor(query.Access(syll_id, props->credibility));
        size_t end_pos = props->end_pos;
        if (!accessor.exhausted()) {
          (*result)[end_pos].push_back(accessor);
        }
        
        TableAccessor log_accessor(accessor);
        string log;
        while (!log_accessor.exhausted()) {
          log += GetEntryText(*(log_accessor.entry())) + " ";
          log_accessor.Next();
        }
        if (!log.empty())
          LOG(ERROR) << "  DFS add entry " << log << " " << end_pos;
        
        if (end_pos < syll_graph.interpreted_length &&
            query.Advance(syll_id, props->credibility)) {
          q.push({end_pos, query});
          query.Backdate();
        }
      }
    }
  }
  return !result->empty();
}

string Table::GetEntryText(const table::Entry& entry) {
  return GetString(entry.text);
}

}  // namespace rime
