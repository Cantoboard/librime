//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-07-10 GONG Chen <chen.sst@gmail.com>
//

#ifndef RIME_VOCABULARY_H_
#define RIME_VOCABULARY_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>

namespace rime {

using Syllabary = set<string>;

using SyllableId = int32_t;

struct IndexCode: public std::array<SyllableId, 3> {
  void clear();
  SyllableId pop_back();
  void push_back(SyllableId syllable_id);
  size_t size() const;
  
  IndexCode::iterator end() {
    return begin() + size_;
  }
  
  IndexCode::const_iterator end() const {
    return begin() + size_;
  }
private:
  size_t size_ = 0;
};

class Code : public vector<SyllableId> {
 public:
  Code() = default;
  Code(const IndexCode& indexCode) {
    size_t new_size = indexCode.size();
    resize(new_size);
    std::copy(indexCode.cbegin(), indexCode.cbegin() + new_size, begin());
  };
  static const size_t kIndexCodeMaxLength = 3;

  bool operator< (const Code& other) const;
  bool operator== (const Code& other) const;

  void CreateIndex(Code* index_code);

  string ToString() const;
};

struct DictEntry {
  string text;
  string comment;
  string preedit;
  double weight = 0.0;
  int commit_count = 0;
  Code code;  // multi-syllable code from prism
  Code override_code;  // For entry with abbrev edges.
  string custom_code;  // user defined code
  int remaining_code_length = 0;

  DictEntry() = default;
  bool operator< (const DictEntry& other) const;
};

class DictEntryList : public vector<of<DictEntry>> {
 public:
  void Sort();
  void SortRange(size_t start, size_t count);
};

using DictEntryFilter = function<bool (an<DictEntry> entry)>;

class DictEntryFilterBinder {
 public:
  virtual ~DictEntryFilterBinder() = default;
  RIME_API virtual void AddFilter(DictEntryFilter filter);

 protected:
  DictEntryFilter filter_;
};

class Vocabulary;

struct VocabularyPage {
  DictEntryList entries;
  an<Vocabulary> next_level;
};

class Vocabulary : public map<int, VocabularyPage> {
 public:
  DictEntryList* LocateEntries(const Code& code);
  void SortHomophones();
};

// word -> { code, ... }
using ReverseLookupTable = map<string, set<string>>;

}  // namespace rime

namespace std {
  template <> struct std::hash<rime::IndexCode> {
    size_t operator()(const rime::IndexCode& code) const;
  };
}

#endif  // RIME_VOCABULARY_H_
