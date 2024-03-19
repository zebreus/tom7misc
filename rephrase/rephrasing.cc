#include "rephrasing.h"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

#include "document.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "crypt/sha256.h"
#include "timer.h"
#include "ansi.h"

#include "llm.h"
#include "models.h"

using Rephrasable = Rephrasing::Rephrasable;

// XXX model, cached database, etc.
Rephrasing::Rephrasing() {}
Rephrasing::~Rephrasing() {}

#define USE_MODEL(name) \
  ContextParams cparams = Models:: name ;       \
  model_key = #name

void Rephrasing::LazyInit() {
  // AnsiInit();
  Timer model_timer;

  // This fits on the GPU, so inference is quite fast.
  USE_MODEL(LLAMA_7B_F16);

  // Best quality.
  // USE_MODEL(LLAMA_70B_F16);

  SamplerParams sparams;
  // These basically don't matter; we do our own sampling.
  sparams.type = SampleType::GREEDY;
  sparams.regex = ".*";

  Timer load_timer;
  llm.reset(new LLM(cparams, sparams));
  printf("Loaded " AWHITE("%s") " in %s.\n",
         model_key.c_str(),
         ANSI::Time(load_timer.Seconds()).c_str());

}

std::string Rephrasing::DatabaseKey(const Rephrasable &rephrasable) {
  // We need the model key.
  LazyInit();
  SHA256::Ctx hash;
  SHA256::Init(&hash);
  SHA256::UpdateString(&hash, model_key);
  // Make sure the context and text can't get mixed up, no matter what
  // they contain.
  SHA256::UpdateString(
      &hash, StringPrintf(".%d.%d.",
                          (int)rephrasable.context.size(),
                          (int)rephrasable.text.size()));
  SHA256::UpdateString(&hash, rephrasable.context);
  SHA256::UpdateString(&hash, rephrasable.text);
  return SHA256::Ascii(SHA256::FinalVector(&hash));
}

#if 0
std::pair<DocTree, double> Rephrasing::NextRephrasing(const DocTree &in) {
  return std::make_pair(in, 0.0);
}
#endif

Rephrasable Rephrasing::GetTextToRephrase(const DocTree &doc) {
  Rephrasable rephrasable;
  std::function<void(const DocTree &)> Rec =
    [&rephrasable, &Rec](const DocTree &doc) -> void {
      if (doc.IsText()) {

        std::string normtext = NormalizeWhitespace(doc.text);
        rephrasable.text += normtext;

      } else {
        if (const std::string *display = doc.GetStringAttr("display")) {
          if (*display == "box") {
            // This is already a box with a fixed size, so we just copy it.
            StringAppendF(&rephrasable.text, "<img src=\"img%d.png\">",
                          (int)rephrasable.images.size());
            rephrasable.images.push_back(std::make_shared<DocTree>(doc));
            return;
          } else if (*display == "span") {
            bool has_style = doc.GetStringAttr("font-face") != nullptr ||
              doc.GetDoubleAttr("font-size") != nullptr ||
              doc.GetBoolAttr("font-bold") != nullptr ||
              doc.GetBoolAttr("font-italic") != nullptr;

            if (has_style) {
              // Process children.
              StringAppendF(&rephrasable.text, "<span class=\"c%d\">",
                            (int)rephrasable.classes.size());
              rephrasable.classes.push_back(doc.attrs);
              for (const std::shared_ptr<DocTree> &child : doc.children) {
                Rec(*child);
              }
              StringAppendF(&rephrasable.text, "</span>");
              return;
            }

          } else {
            LOG(FATAL) << "Unknown display: " << *display;
          }
        }

        // Groups and unstyled spans are ignored.
        for (const std::shared_ptr<DocTree> &child : doc.children) {
          Rec(*child);
        }
      }
    };

  return rephrasable;
}
