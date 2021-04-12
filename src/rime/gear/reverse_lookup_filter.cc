//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2013-11-05 GONG Chen <chen.sst@gmail.com>
//
#include <utf8.h>
#include <rime/candidate.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>
#include <rime/dict/reverse_lookup_dictionary.h>
#include <rime/gear/reverse_lookup_filter.h>
#include <rime/gear/translator_commons.h>

namespace rime {

class ReverseLookupFilterTranslation : public CacheTranslation {
 public:
  ReverseLookupFilterTranslation(an<Translation> translation,
                                 ReverseLookupFilter* filter)
      : CacheTranslation(translation), filter_(filter) {
  }
  virtual an<Candidate> Peek();

 protected:
  ReverseLookupFilter* filter_;
};

an<Candidate> ReverseLookupFilterTranslation::Peek() {
  auto cand = CacheTranslation::Peek();
  if (cand) {
    filter_->Process(cand);
  }
  return cand;
}

ReverseLookupFilter::ReverseLookupFilter(const Ticket& ticket)
    : Filter(ticket), TagMatching(ticket) {
  if (ticket.name_space == "filter") {
    name_space_ = "reverse_lookup";
  }
}

void ReverseLookupFilter::Initialize() {
  initialized_ = true;
  if (!engine_)
    return;
  Ticket ticket(engine_, name_space_);
  if (auto c = ReverseLookupDictionary::Require("reverse_lookup_dictionary")) {
    rev_dict_.reset(c->Create(ticket));
    if (rev_dict_ && !rev_dict_->Load()) {
      rev_dict_.reset();
    }
  }
  if (Config* config = engine_->schema()->config()) {
    config->GetBool(name_space_ + "/overwrite_comment", &overwrite_comment_);
    comment_formatter_.Load(config->GetList(name_space_ + "/comment_format"));
  }
}

an<Translation> ReverseLookupFilter::Apply(
    an<Translation> translation, CandidateList* candidates) {
  if (!initialized_) {
    Initialize();
  }
  if (!rev_dict_) {
    return translation;
  }
  return New<ReverseLookupFilterTranslation>(translation, this);
}

void ReverseLookupFilter::Process(const an<Candidate>& cand) {
  if (!overwrite_comment_ && !cand->comment().empty())
    return;
  auto phrase = As<Phrase>(Candidate::GetGenuineCandidate(cand));
  if (!phrase)
    return;
  string codes;
  if (rev_dict_->ReverseLookup(phrase->text(), &codes)) {
    comment_formatter_.Apply(&codes);
    if (!codes.empty()) {
      phrase->set_comment(codes);
    }
  } else {
    // ReverseLookup the phrase word by word.
    string cur_word_codes;
    const char* pp = phrase->text().c_str();

    uint32_t cp;
    while ((cp = utf8::unchecked::next(pp)), cp) {
      string word_in_phrase;
      utf8::unchecked::append(cp, word_in_phrase.begin());
      // printf("UFO word_in_phrase=%s\n", word_in_phrase.c_str());
      if (rev_dict_->ReverseLookup(word_in_phrase, &cur_word_codes)) {
        comment_formatter_.Apply(&cur_word_codes);
        // printf("UFO phase rev lookup %s %s\n", word_in_phrase.c_str(), cur_word_codes.c_str());
        if (!codes.empty()) { codes.append(" "); }
        codes.append(cur_word_codes);
      }
    }
    if (!codes.empty()) {
      phrase->set_comment(codes);
    }
  }
}

}  // namespace rime
