#pragma once

#include <cstddef>

namespace hoskey::engine {

inline constexpr PrototypeTransitionRecord kPrototypeVerifiedTransitionRecords[] = {
    {"авив", true, true, "ае", "аеуы"},
    {"алв", true, true, "", "икюя"},
    {"анч", true, true, "", "а"},
    {"аня", true, true, "", "о"},
    {"ася", true, true, "", "нт"},
    {"бои", true, true, "т", "аоу"},
    {"боно", true, true, "вм", "аи"},
    {"брач", true, true, "кн", "а"},
    {"бэко", true, true, "н", "аец"},
    {"взре", true, true, "вз", "л"},
    {"евф", true, true, "р", "а"},
    {"куэ", true, true, "", "я"},
    {"кыш", true, true, "т", "и"},
    {"уня", true, true, "л", "ийюя"},
    {"фог", true, true, "е", "аы"},
    {"цык", true, true, "а", "н"},
    {"чия", true, true, "м", "ащы"},
    {"шты", true, true, "кр", "а"},
    {"эрк", true, true, "у", "йля"},
    {"яу", true, true, "з", "о"},
};

inline constexpr std::size_t kPrototypeVerifiedTransitionRecordCount =
    sizeof(kPrototypeVerifiedTransitionRecords) / sizeof(kPrototypeVerifiedTransitionRecords[0]);

}  // namespace hoskey::engine
