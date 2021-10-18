//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-04-24 GONG Chen <chen.sst@gmail.com>
//
#include <cstring>
#include <iostream>
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/composition.h>
#include <rime/context.h>
#include <rime/deployer.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/menu.h>
#include <rime/schema.h>
#include <rime/setup.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/dict_compiler.h>
#include <rime/lever/deployment_tasks.h>

#include <sys/time.h>

using namespace rime;

class RimeConsole {
 public:
  RimeConsole() : interactive_(false),
                  engine_(Engine::Create()) {
    conn_ = engine_->sink().connect(
        [this](const string& x) { OnCommit(x); });
  }
  ~RimeConsole() {
    conn_.disconnect();
  }

  void OnCommit(const string &commit_text) {
    if (interactive_) {
      std::cout << "commit : [" << commit_text << "]" << std::endl;
    }
    else {
      std::cout << commit_text << std::endl;
    }
  }

  void PrintComposition(const Context *ctx) {
    if (!ctx || !ctx->IsComposing())
      return;
    std::cout << "input  : [" << ctx->input() << "]" << std::endl;
    const Composition &comp = ctx->composition();
    if (comp.empty())
      return;
    std::cout << "comp.  : [" << comp.GetDebugText() << "]" << std::endl;
    const Segment &current(comp.back());
    if (!current.menu)
      return;
    int page_size = engine_->active_engine()->schema()->page_size();
    int page_no = current.selected_index / page_size;
    the<Page> page(current.menu->CreatePage(page_size, page_no));
    if (!page)
      return;
    std::cout << "page_no: " << page_no
              << ", index: " << current.selected_index << std::endl;
    int i = 0;
    for (const an<Candidate> &cand : page->candidates) {
      std::cout << "cand. " << (++i % 10) <<  ": [";
      std::cout << cand->text();
      std::cout << "]";
      if (!cand->comment().empty())
        std::cout << "  " << cand->comment();
      std::cout << "  quality=" << cand->quality();
      std::cout << std::endl;
    }
  }

  void ProcessLine(const string &line) {
    KeySequence keys;
    if (!keys.Parse(line)) {
      LOG(ERROR) << "error parsing input: '" << line << "'";
      return;
    }
    for (const KeyEvent &key : keys) {
      engine_->ProcessKey(key);
    }
    Context *ctx = engine_->active_engine()->context();
    if (interactive_) {
      PrintComposition(ctx);
    }
    else {
      PrintComposition(ctx);
      if (ctx && ctx->IsComposing()) {
        ctx->Commit();
      }
    }
  }

  void set_interactive(bool interactive) { interactive_ = interactive; }
  bool interactive() const { return interactive_; }

 private:
  bool interactive_;
  the<Engine> engine_;
  connection conn_;
};

extern std::unordered_map<std::string, int> query_dup;
/*
namespace rime {
extern int cachceHit;
extern int cacheMiss;
}*/

struct ProfileTimer {
private:
  unsigned long& counter;
  unsigned long start_time;
  
  unsigned long get_timestamp() {
    struct timeval now;
    int rv = gettimeofday(&now, NULL);
    return now.tv_sec * 1000 + now.tv_usec / 1000;
  }

public:
  ProfileTimer(unsigned long& counter): counter(counter), start_time(get_timestamp()) {
  }
  
  ~ProfileTimer() {
    counter += get_timestamp() - start_time;
  }
};

static void profile(RimeConsole& console, string input) {
  unsigned long counter = 0;
  int retry_count = 10;
  {
    ProfileTimer timer(counter);
    for (int i = 0; i < retry_count; ++ i)
      console.ProcessLine(input);
  }
  std::cerr << input.length() << " " << counter / retry_count << "ms " << counter / retry_count / input.length() << std::endl;
}

// program entry
int main(int argc, char *argv[]) {
  // initialize la Rime
  SetupLogging("rime.console");
  LoadModules(kDefaultModules);

  Deployer deployer;
  /*deployer.prebuilt_data_dir = "/Users/alexman/workspace/personal/librime-build-special-build/test_ground_canton";
  deployer.user_data_dir = "/Users/alexman/workspace/personal/librime-build-special-build/test_ground_canton";
  deployer.staging_dir = "/Users/alexman/workspace/personal/librime-build-special-build/test_ground_canton/build";*/
  chdir("/Users/alexman/workspace/personal/librime-build-special-build/test_ground_canton");
  InstallationUpdate installation;
  if (!installation.Run(&deployer)) {
    std::cerr << "failed to initialize installation." << std::endl;
    return 1;
  }
  std::cerr << "initializing...";
  WorkspaceUpdate workspace_update;
  if (!workspace_update.Run(&deployer)) {
    std::cerr << "failure!" << std::endl;
    return 1;
  }
  std::cerr << "ready." << std::endl;

  RimeConsole console;
  // "-i" turns on interactive mode (no commit at the end of line)
  bool interactive = argc > 1 && !strcmp(argv[1], "-i");
  console.set_interactive(interactive);
  
  //console.ProcessLine("diu");
  //console.ProcessLine("diu");
  /*console.ProcessLine("di");
  console.ProcessLine("diunei");
  console.ProcessLine("diu");
  console.ProcessLine("di");*/
  
  console.ProcessLine("diuneiloumouhai");
  //console.ProcessLine("diuneilaa");
  //console.ProcessLine("diulaasing");
  
  /*
  profile(console, "diulaamaa");
  profile(console, "gfhgsdgm");
  
  string s = "s";
  for (int i = 0; i < 16; ++i) {
    profile(console, s);
    s += "s";
  }*/
  
  /*
  profile(console, "abcdefgh");
  profile(console, "dnlmhgccnlmstp");
  profile(console, "ghksfdghls");
  profile(console, "ghfgjkawuiwert");
  profile(console, "sssssssssssssss");
  profile(console, "lllllllllllllll");
  profile(console, "ddddddddddddddd");
   */
  
  //std::cerr << "hit: " << rime::cachceHit << " miss: " << rime::cacheMiss << " ms " << counter << std::endl;
  return 0;
  
  // process input
  string line;
  while (std::cin) {
    std::getline(std::cin, line);
    console.ProcessLine(line);
  }
  return 0;
}
