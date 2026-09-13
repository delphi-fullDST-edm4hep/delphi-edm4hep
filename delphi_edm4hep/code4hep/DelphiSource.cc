#include "delphi_edm4hep/ConversionPipeline.h"
#include "delphi_edm4hep/BankPrefix.h"
#include "delphi_edm4hep/PhdstHarness.h"

#include "Code4hep/IOUtilities/fillProductRegistry.h"
#include "Code4hep/IOUtilities/FrameParameterConversion.h"
#include "Code4hep/IOUtilities/putOnReadForAllProducts.h"
#include "DataFormats/Provenance/interface/EventAuxiliary.h"
#include "DataFormats/Provenance/interface/EventID.h"
#include "DataFormats/Provenance/interface/HardwareResourcesDescription.h"
#include "DataFormats/Provenance/interface/LuminosityBlockAuxiliary.h"
#include "DataFormats/Provenance/interface/ProcessConfiguration.h"
#include "DataFormats/Provenance/interface/ProcessHistory.h"
#include "DataFormats/Provenance/interface/ProcessHistoryRegistry.h"
#include "DataFormats/Provenance/interface/RunAuxiliary.h"
#include "DataFormats/Provenance/interface/Timestamp.h"
#include "FWCore/Framework/interface/EventPrincipal.h"
#include "FWCore/Framework/interface/FileBlock.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/InputSource.h"
#include "FWCore/Framework/interface/InputSourceMacros.h"
#include "FWCore/Framework/interface/LuminosityBlockPrincipal.h"
#include "FWCore/Framework/interface/RunPrincipal.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDMException.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace delphi_edm4hep::code4hep {
namespace {

std::atomic_bool sourceActive{false};

struct FrameRecord {
  podio::Frame frame;
  int run;
  int event;
};

// PHDST calls user02 from inside its blocking Fortran event loop. Code4hep
// pulls events from InputSource. A one-frame rendezvous bridges those two
// contracts while keeping every DELPHI package call on one worker thread.
class FrameStream {
public:
  explicit FrameStream(harness::Config config) {
    config.frame_sink = [this](podio::Frame&& frame, int run, int event) {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queued_; });
      if (stopping_) return false;

      queued_.emplace(FrameRecord{std::move(frame), run, event});
      ready_.notify_all();
      ready_.wait(lock, [this] { return stopping_ || !queued_; });
      return !stopping_;
    };

    worker_ = std::thread([this, config = std::move(config)] {
      int status = 3;
      std::exception_ptr failure;
      try {
        status = harness::run(config);
      } catch (...) {
        failure = std::current_exception();
      }
      {
        std::lock_guard lock(mutex_);
        status_ = status;
        failure_ = std::move(failure);
        finished_ = true;
      }
      ready_.notify_all();
    });
  }

  FrameStream(FrameStream const&) = delete;
  FrameStream& operator=(FrameStream const&) = delete;

  ~FrameStream() { stop(); }

  std::optional<FrameRecord> next() {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [this] { return queued_ || finished_; });
    if (queued_) {
      auto record = std::move(queued_);
      queued_.reset();
      ready_.notify_all();
      return record;
    }
    if (failure_) std::rethrow_exception(failure_);
    if (status_ != 0) {
      throw std::runtime_error("PHDST conversion stopped with status " +
                               std::to_string(status_));
    }
    return std::nullopt;
  }

  void stop() noexcept {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      queued_.reset();
    }
    ready_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::optional<FrameRecord> queued_;
  std::thread worker_;
  std::exception_ptr failure_;
  int status_ = 3;
  bool finished_ = false;
  bool stopping_ = false;
};

harness::InputMode parseInputMode(std::string const& mode) {
  if (mode == "file") return harness::InputMode::File;
  if (mode == "nickname") return harness::InputMode::Nickname;
  if (mode == "pdl") return harness::InputMode::Pdl;
  throw cms::Exception("Configuration")
      << "DelphiSource inputMode must be file, nickname, or pdl; got '"
      << mode << "'";
}

}  // namespace

class DelphiSource final : public edm::InputSource {
public:
  DelphiSource(edm::ParameterSet const&, edm::InputSourceDescription const&);
  ~DelphiSource() noexcept override;

  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  ItemTypeInfo getNextItemType() override;
  std::shared_ptr<edm::RunAuxiliary> readRunAuxiliary_() override;
  std::shared_ptr<edm::LuminosityBlockAuxiliary> readLuminosityBlockAuxiliary_() override;
  std::shared_ptr<edm::FileBlock> readFile_() override;
  void closeFile_() override;
  void readRun_(edm::RunPrincipal&) override;
  void readLuminosityBlock_(edm::LuminosityBlockPrincipal&) override;
  void readEvent_(edm::EventPrincipal&) override;

  bool acquireNextFrame();
  void registerFrameProducts();

  std::unique_ptr<FrameStream> stream_;
  std::optional<FrameRecord> current_;
  std::shared_ptr<edm::ProductRegistry const> inputRegistry_;
  edm::ProcessHistoryID processHistoryID_;
  ItemType nextItemType_ = ItemType::IsInvalid;
  edm::RunNumber_t nextRun_ = edm::invalidRunNumber;
  edm::EventNumber_t nextEvent_ = edm::invalidEventNumber;
  bool firstFile_ = true;
  bool isRealData_ = true;
  bool ownsSourceLease_ = false;
};

DelphiSource::DelphiSource(edm::ParameterSet const& pset,
                           edm::InputSourceDescription const& description)
    : InputSource(pset, description),
      isRealData_(pset.getUntrackedParameter<bool>("isRealData")) {
  if (sourceActive.exchange(true)) {
    throw cms::Exception("Configuration")
        << "Only one DelphiSource may exist in a process because PHDST and "
           "the DELPHI COMMON blocks are process-global";
  }
  ownsSourceLease_ = true;

  try {
    harness::Config config;
    const std::string input = pset.getUntrackedParameter<std::string>("input");
    config.input_mode = parseInputMode(
        pset.getUntrackedParameter<std::string>("inputMode"));
    if (config.input_mode == harness::InputMode::Nickname) {
      config.input_nickname = input;
    } else {
      config.input = input;
    }

    const std::string pass =
        pset.getUntrackedParameter<std::string>("conversionPass");
    const auto intermediates =
        pset.getUntrackedParameter<std::vector<std::string>>("intermediateFiles");
    if (pass == "sdst") {
      if (!intermediates.empty()) {
        throw cms::Exception("Configuration")
            << "DelphiSource conversionPass='sdst' does not accept intermediateFiles";
      }
      pipeline::configureSdst(config);
    } else if (pass == "fdst") {
      if (config.input_mode != harness::InputMode::File) {
        throw cms::Exception("Configuration")
            << "DelphiSource conversionPass='fdst' currently requires inputMode='file'";
      }
      if (intermediates.empty()) {
        throw cms::Exception("Configuration")
            << "DelphiSource conversionPass='fdst' requires at least one intermediate file";
      }
      config.input_edm4hep = intermediates.front();
      for (auto it = std::next(intermediates.begin()); it != intermediates.end(); ++it) {
        config.input_edm4hep_extra.emplace_back(*it);
      }
      pipeline::configureFdst(config);
    } else {
      throw cms::Exception("Configuration")
          << "DelphiSource conversionPass must be 'sdst' or 'fdst'; got '"
          << pass << "'";
    }

    stream_ = std::make_unique<FrameStream>(std::move(config));
    if (!acquireNextFrame()) {
      throw edm::Exception(edm::errors::FileReadError, "DelphiSource")
          << "The DELPHI input contained no convertible events";
    }
    registerFrameProducts();
  } catch (...) {
    stream_.reset();
    sourceActive = false;
    ownsSourceLease_ = false;
    throw;
  }
}

DelphiSource::~DelphiSource() noexcept {
  stream_.reset();
  if (ownsSourceLease_) sourceActive = false;
}

bool DelphiSource::acquireNextFrame() {
  try {
    current_ = stream_->next();
  } catch (std::exception const& error) {
    throw edm::Exception(edm::errors::FileReadError, "DelphiSource")
        << error.what();
  }
  if (!current_) return false;

  const auto run = current_->frame.getParameter<int>(
      delphi_edm4hep::bank::make(delphi_edm4hep::bank::Pass::Sdst,
                                 "EVT", "runNumber"));
  const auto event = current_->frame.getParameter<int>(
      delphi_edm4hep::bank::make(delphi_edm4hep::bank::Pass::Sdst,
                                 "EVT", "eventNumber"));
  if (!run || !event || *run != current_->run || *event != current_->event) {
    throw edm::Exception(edm::errors::EventCorruption, "DelphiSource")
        << "Converted frame identity does not match the PHDST record";
  }
  c4h::materializeFrameParameters(current_->frame);
  return true;
}

void DelphiSource::registerFrameProducts() {
  // Input products must not use the current process name (the framework
  // rejects duplicate process names in a ProcessHistory). Keep their origin
  // distinct in the same way that PodioSource uses "PODIO".
  constexpr char processName[] = "DELPHIINPUT";
  edm::ParameterSet processParameters;
  processParameters.registerIt();
  edm::ProcessConfiguration processConfiguration(
      processName, processParameters.id(), "", edm::HardwareResourcesDescription());
  edm::ProcessHistory processHistory;
  processHistory.push_back(processConfiguration);
  processHistory.setProcessHistoryID();
  processHistoryRegistryForUpdate().registerProcessHistory(processHistory);
  processHistoryID_ = processHistory.id();

  auto registry = c4h::fillProductRegistry(current_->frame, processName, false);
  inputRegistry_.reset(registry.release());
  std::vector<std::string> processOrder;
  edm::processingOrderMerge(processHistoryRegistry(), processOrder);
  productRegistryUpdate().updateFromInput(inputRegistry_->productList(), processOrder);
}

DelphiSource::ItemTypeInfo DelphiSource::getNextItemType() {
  if (firstFile_) return ItemTypeInfo::isFile();

  if (nextItemType_ == ItemType::IsEvent || nextItemType_ == ItemType::IsInvalid) {
    if (nextItemType_ == ItemType::IsEvent && !acquireNextFrame()) {
      return ItemTypeInfo::isStop();
    }
    if (!current_) return ItemTypeInfo::isStop();

    const auto run = static_cast<edm::RunNumber_t>(current_->run);
    nextEvent_ = static_cast<edm::EventNumber_t>(current_->event);
    if (nextEvent_ == edm::invalidEventNumber || run == edm::invalidRunNumber) {
      throw edm::Exception(edm::errors::EventCorruption, "DelphiSource")
          << "Invalid run/event identity " << run << ':' << nextEvent_;
    }
    if (nextItemType_ == ItemType::IsInvalid || run != nextRun_) {
      nextRun_ = run;
      nextItemType_ = ItemType::IsRun;
    } else {
      nextItemType_ = ItemType::IsEvent;
    }
  } else if (nextItemType_ == ItemType::IsRun) {
    nextItemType_ = ItemType::IsLumi;
  } else if (nextItemType_ == ItemType::IsLumi) {
    nextItemType_ = ItemType::IsEvent;
  }
  return ItemTypeInfo(nextItemType_);
}

std::shared_ptr<edm::RunAuxiliary> DelphiSource::readRunAuxiliary_() {
  auto auxiliary = std::make_shared<edm::RunAuxiliary>(
      nextRun_, edm::Timestamp::invalidTimestamp(), edm::Timestamp::invalidTimestamp());
  auxiliary->setProcessHistoryID(processHistoryID_);
  return auxiliary;
}

std::shared_ptr<edm::LuminosityBlockAuxiliary>
DelphiSource::readLuminosityBlockAuxiliary_() {
  auto auxiliary = std::make_shared<edm::LuminosityBlockAuxiliary>(
      nextRun_, 1U, edm::Timestamp::invalidTimestamp(),
      edm::Timestamp::invalidTimestamp());
  auxiliary->setProcessHistoryID(processHistoryID_);
  return auxiliary;
}

std::shared_ptr<edm::FileBlock> DelphiSource::readFile_() {
  firstFile_ = false;
  return std::make_shared<edm::FileBlock>();
}

void DelphiSource::closeFile_() {}

void DelphiSource::readRun_(edm::RunPrincipal& principal) {
  principal.fillRunPrincipal(processHistoryRegistry());
}

void DelphiSource::readLuminosityBlock_(edm::LuminosityBlockPrincipal& principal) {
  auto history = processHistoryRegistry().getMapped(principal.aux().processHistoryID());
  principal.fillLuminosityBlockPrincipal(history);
}

void DelphiSource::readEvent_(edm::EventPrincipal& principal) {
  edm::EventAuxiliary auxiliary(edm::EventID(nextRun_, 1U, nextEvent_),
                                processGUID(),
                                edm::Timestamp::invalidTimestamp(),
                                isRealData_);
  auxiliary.setProcessHistoryID(processHistoryID_);
  auto history = processHistoryRegistry().getMapped(processHistoryID_);
  principal.fillEventPrincipal(auxiliary, history);
  c4h::putOnReadForAllProducts(current_->frame, *inputRegistry_, principal);
  current_.reset();
}

void DelphiSource::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.addUntracked<std::string>("input")
      ->setComment("DELPHI file path, dataset nickname, or PDL file");
  desc.addUntracked<std::string>("inputMode", "file")
      ->setComment("file, nickname, or pdl");
  desc.addUntracked<std::string>("conversionPass", "sdst")
      ->setComment("sdst or fdst");
  desc.addUntracked<std::vector<std::string>>("intermediateFiles", {})
      ->setComment("Pass-1 EDM4hep inputs required by pass='fdst'");
  desc.addUntracked<bool>("isRealData", true);
  edm::InputSource::fillDescription(desc);
  descriptions.add("source", desc);
}

}  // namespace delphi_edm4hep::code4hep

using delphi_edm4hep::code4hep::DelphiSource;
DEFINE_FWK_INPUT_SOURCE(DelphiSource);
