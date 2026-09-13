#include "delphi_edm4hep/PhdstHarness.h"

#include "FWCore/Concurrency/interface/ThreadsController.h"
#include "FWCore/Concurrency/interface/setNThreads.h"
#include "FWCore/Framework/interface/EventProcessor.h"
#include "FWCore/Framework/interface/defaultCmsRunServices.h"
#include "FWCore/MessageLogger/interface/JobReport.h"
#include "FWCore/PluginManager/interface/PluginManager.h"
#include "FWCore/PluginManager/interface/PresenceFactory.h"
#include "FWCore/PluginManager/interface/standard.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ProcessDesc.h"
#include "FWCore/ParameterSet/interface/ThreadsInfo.h"
#include "FWCore/ServiceRegistry/interface/ServiceRegistry.h"
#include "FWCore/ServiceRegistry/interface/ServiceToken.h"
#include "FWCore/ServiceRegistry/interface/ServiceWrapper.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/Presence.h"

#include "oneapi/tbb/task_arena.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Stitched 2026-08-21 exports ParameterSetReader but accidentally omits this
// one public declaration from its installed headers.  Keep the exact exported
// signature here until that packaging omission is fixed upstream.
namespace edm {
std::unique_ptr<ParameterSet> readConfig(
    std::string const&, std::vector<std::string> const&);
}

extern "C" {
void phdst_();

void user00_() noexcept { delphi_edm4hep::harness::on_user00(); }
void user01_(int* need) noexcept { delphi_edm4hep::harness::on_user01(need); }
void user02_() noexcept { delphi_edm4hep::harness::on_user02(); }
void user99_() noexcept { delphi_edm4hep::harness::on_user99(); }
}

namespace {

class TaskCleanup {
public:
  explicit TaskCleanup(edm::EventProcessor& processor) : processor_(processor) {}
  ~TaskCleanup() { processor_.taskCleanup(); }

private:
  edm::EventProcessor& processor_;
};

int run(const char* config, std::vector<std::string> const& arguments) {
  edmplugin::PluginManager::configure(edmplugin::standard::config());
  auto messageService = std::shared_ptr<edm::Presence>(
      edm::PresenceFactory::get()->makePresence("SingleThreadMSPresence").release());

  auto parameters = edm::readConfig(config, arguments);
  auto process = std::make_shared<edm::ProcessDesc>(std::move(parameters));
  process->addServices(edm::defaultCmsRunServices());

  auto threads = edm::threadOptions(*process->getProcessPSet());
  std::unique_ptr<edm::ThreadsController> threadController;
  threads.nThreads_ =
      edm::setNThreads(threads.nThreads_, threads.stackSize_, threadController);
  edm::setThreadOptions(threads, *process->getProcessPSet());

  auto report = std::make_shared<edm::serviceregistry::ServiceWrapper<edm::JobReport>>(
      std::make_unique<edm::JobReport>(nullptr));
  auto services = edm::ServiceRegistry::createContaining(report);

  edm::EventProcessor processor(
      process, services, edm::serviceregistry::kOverlapIsError);
  TaskCleanup cleanup(processor);
  processor.beginJob();
  oneapi::tbb::task_arena{static_cast<int>(threads.nThreads_)}.execute(
      [&processor] { (void)processor.runToCompletion(); });
  processor.endJob();
  return 0;
}

}  // namespace

int main(int argc, const char* argv[]) {
  // This reference makes the static PHDST entry point (and its transitive
  // archive dependencies) part of the executable even though DelphiSource
  // calls it from the converter shared library.
  [[maybe_unused]] void (*volatile phdstEntry)() = &phdst_;

  if (argc < 2) {
    std::cerr << "usage: delphiRun CONFIG.py [CONFIG-ARG ...]\n";
    return 2;
  }

  try {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    return run(argv[1], arguments);
  } catch (cms::Exception const& error) {
    std::cerr << error.explainSelf() << '\n';
  } catch (std::exception const& error) {
    std::cerr << "delphiRun: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "delphiRun: unknown exception\n";
  }
  return 1;
}
