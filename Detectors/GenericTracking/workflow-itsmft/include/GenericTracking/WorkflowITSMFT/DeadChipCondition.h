// Copyright 2019-2026 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file DeadChipCondition.h
/// \brief The ITS's and the MFT's dead chips per timeframe: the detector's
///        time-dependent dead map (`<DET>/Calib/TimeDeadMap`) as a condition
///        input of their `Conditions` (ConditionsITSMFT.h).
///
/// The map holds, per orbit key, the chips dead from that orbit on, and a
/// static part. A timeframe takes the entry of the highest key at or below
/// its first orbit (the first entry before it), as `TimeDeadMap::getMapAtOrbit`
/// does; an entry further than the allowed gap is dropped, the static part
/// kept. The list is decoded again only when that choice changes, which is
/// rarely within a run.
///
/// The chips are O2's chip IDs, which are the layout's `SensorID`s
/// (`alice::placeAlpides`): what the tracking reads needs no translation.

#ifndef GTRK_WORKFLOWITSMFT_DEADCHIPCONDITION_H_
#define GTRK_WORKFLOWITSMFT_DEADCHIPCONDITION_H_

#include "GenericTracking/Base/Definitions.h"
#include "GenericTracking/Base/LayerSpec.h"
#include "GenericTracking/Workflow/DataSpecs.h"

#include "DataFormatsITSMFT/TimeDeadMap.h"
#include "Framework/CCDBParamSpec.h"
#include "Framework/ConcreteDataMatcher.h"
#include "Framework/InputRecord.h"
#include "Framework/InputSpec.h"
#include "Framework/ProcessingContext.h"

#include <fairlogger/Logger.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace o2::gtrk::workflow
{

/// The CCDB path of \a Det's time-dependent dead map.
template <DetectorLayout Det>
[[nodiscard]] std::string timeDeadMapPath()
{
  return detectorOrigin<Det>().template as<std::string>() + "/Calib/TimeDeadMap";
}

/// \a Det's dead map as a condition input, and the sensors dead at an orbit.
template <DetectorLayout Det>
class DeadChipCondition
{
 public:
  /// Declares the input.
  static void declare(std::vector<o2::framework::InputSpec>& inputs) { inputs.emplace_back(inputBinding<Det>(kBinding), detectorOrigin<Det>(), "TIMEDEADMAP", 0, o2::framework::Lifetime::Condition, o2::framework::ccdbParamSpec(timeDeadMapPath<Det>())); }

  /// At the top of the device's run: delivers a new one through finaliseCCDB.
  static void checkUpdates(o2::framework::ProcessingContext& pc) { pc.inputs().get<o2::itsmft::TimeDeadMap*>(inputBinding<Det>(kBinding).c_str()); }

  /// Whether \a matcher is the dead map, which is then taken.
  bool finaliseCCDB(o2::framework::ConcreteDataMatcher& matcher, void* obj)
  {
    if (!(matcher == o2::framework::ConcreteDataMatcher(detectorOrigin<Det>(), "TIMEDEADMAP", 0))) {
      return false;
    }
    mMap = static_cast<const o2::itsmft::TimeDeadMap*>(obj);
    mKeys = mMap->getEvolvingMapKeys(); // a std::map's keys: sorted
    mDecodedKey.reset();                // a new object: decoded again on the next call
    LOGP(info, "{}'s time dead map from the CCDB ({}): version {}, {} time-dependent entries", Det::name, timeDeadMapPath<Det>(), mMap->getMapVersion(), mKeys.size());
    return true;
  }

  /// The sensors dead at \a orbit: the entry for it, and with
  /// \a includeStatic the static part; an entry more than \a orbitGap orbits
  /// away is dropped (negative: never). Empty before the map arrives. Valid
  /// until the next call.
  [[nodiscard]] std::span<const SensorID> deadSensors(std::uint32_t orbit, bool includeStatic, long orbitGap)
  {
    if (mMap == nullptr) {
      return {};
    }
    const long key = entryKey(orbit, orbitGap);
    if (mDecodedKey == key && mDecodedStatic == includeStatic) {
      return mDead; // almost every timeframe
    }
    const auto chips = mMap->getDeadChipsAt(orbit, includeStatic, orbitGap);
    mDead.assign(chips.begin(), chips.end());
    // The static part and a range may both hold a chip.
    std::ranges::sort(mDead);
    mDead.erase(std::ranges::unique(mDead).begin(), mDead.end());
    mDecodedKey = key;
    mDecodedStatic = includeStatic;
    LOGP(info, "{}: {} dead chips from orbit {} on ({}the static part, entry {})", Det::name, mDead.size(), orbit, includeStatic ? "with " : "without ", key < 0 ? std::string{"none within the gap"} : std::to_string(key));
    return mDead;
  }

 private:
  static constexpr const char* kBinding = "gtrkTimeDeadMap";

  /// The time-dependent entry \a orbit takes, as `getMapAtOrbit` picks it;
  /// -1 for none: no entries, or the nearest beyond \a orbitGap.
  [[nodiscard]] long entryKey(std::uint32_t orbit, long orbitGap) const
  {
    if (mKeys.empty()) {
      return -1;
    }
    const auto above = std::ranges::upper_bound(mKeys, static_cast<unsigned long>(orbit));
    const auto picked = static_cast<long>(above == mKeys.begin() ? *above : *std::prev(above));
    return orbitGap >= 0 && std::abs(static_cast<long>(orbit) - picked) > orbitGap ? -1 : picked;
  }

  const o2::itsmft::TimeDeadMap* mMap = nullptr; ///< the CCDB cache's
  std::vector<unsigned long> mKeys;              ///< the time-dependent entries' orbits, sorted
  std::vector<SensorID> mDead;                   ///< the last decoded list, sorted
  std::optional<long> mDecodedKey;               ///< the entry mDead was decoded for; none before the first
  bool mDecodedStatic = false;
};

} // namespace o2::gtrk::workflow

#endif // GTRK_WORKFLOWITSMFT_DEADCHIPCONDITION_H_
