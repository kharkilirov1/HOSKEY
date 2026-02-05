/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "suggest/core/result/suggestion_results.h"

#include <algorithm>
#include <vector>

#include "napi_helpers.h"
#include "suggest/core/dictionary/dictionary.h"

namespace latinime {

// Helper functions for NAPI (equivalent to JniDataUtils)
namespace {

void outputCodePoints(napi_env env, napi_value outputArray, const int start,
        const int maxLength, const int *const codePoints, const int codePointCount,
        const bool needsNullTermination) {
    const int codePointsToWrite = std::min(maxLength, codePointCount);
    for (int i = 0; i < codePointsToWrite; ++i) {
        napi_value val;
        napi_create_int32(env, codePoints[i], &val);
        napi_set_element(env, outputArray, start + i, val);
    }
    if (needsNullTermination && codePointsToWrite < maxLength) {
        napi_value nullVal;
        napi_create_int32(env, 0, &nullVal);
        napi_set_element(env, outputArray, start + codePointsToWrite, nullVal);
    }
}

void putIntToArray(napi_env env, napi_value array, const int index, const int value) {
    napi_value val;
    napi_create_int32(env, value, &val);
    napi_set_element(env, array, index, val);
}

void putFloatToArray(napi_env env, napi_value array, const int index, const float value) {
    napi_value val;
    napi_create_double(env, static_cast<double>(value), &val);
    napi_set_element(env, array, index, val);
}

} // anonymous namespace

void SuggestionResults::outputSuggestions(napi_env env, napi_value outSuggestionCount,
        napi_value outputCodePointsArray, napi_value outScoresArray, napi_value outSpaceIndicesArray,
        napi_value outTypesArray, napi_value outAutoCommitFirstWordConfidenceArray,
        napi_value outWeightOfLangModelVsSpatialModel) {
    int outputIndex = 0;
    while (!mSuggestedWords.empty()) {
        const SuggestedWord &suggestedWord = mSuggestedWords.top();
        const int start = outputIndex * MAX_WORD_LENGTH;
        outputCodePoints(env, outputCodePointsArray, start,
                MAX_WORD_LENGTH /* maxLength */, suggestedWord.getCodePoint(),
                suggestedWord.getCodePointCount(), true /* needsNullTermination */);
        putIntToArray(env, outScoresArray, outputIndex, suggestedWord.getScore());
        putIntToArray(env, outSpaceIndicesArray, outputIndex,
                suggestedWord.getIndexToPartialCommit());
        putIntToArray(env, outTypesArray, outputIndex, suggestedWord.getType());
        if (mSuggestedWords.size() == 1) {
            putIntToArray(env, outAutoCommitFirstWordConfidenceArray, 0 /* index */,
                    suggestedWord.getAutoCommitFirstWordConfidence());
        }
        ++outputIndex;
        mSuggestedWords.pop();
    }
    putIntToArray(env, outSuggestionCount, 0 /* index */, outputIndex);
    putFloatToArray(env, outWeightOfLangModelVsSpatialModel, 0 /* index */,
            mWeightOfLangModelVsSpatialModel);
}

void SuggestionResults::addPrediction(const int *const codePoints, const int codePointCount,
        const int probability) {
    if (probability == NOT_A_PROBABILITY) {
        // Invalid word.
        return;
    }
    addSuggestion(codePoints, codePointCount, probability, Dictionary::KIND_PREDICTION,
            NOT_AN_INDEX, NOT_A_FIRST_WORD_CONFIDENCE);
}

void SuggestionResults::addSuggestion(const int *const codePoints, const int codePointCount,
        const int score, const int type, const int indexToPartialCommit,
        const int autoCommitFirstWordConfidence) {
    if (codePointCount <= 0 || codePointCount > MAX_WORD_LENGTH) {
        // Invalid word.
        AKLOGE("Invalid word is added to the suggestion results. codePointCount: %d",
                codePointCount);
        return;
    }
    if (getSuggestionCount() >= mMaxSuggestionCount) {
        const SuggestedWord &mWorstSuggestion = mSuggestedWords.top();
        if (score > mWorstSuggestion.getScore() || (score == mWorstSuggestion.getScore()
                && codePointCount < mWorstSuggestion.getCodePointCount())) {
            mSuggestedWords.pop();
        } else {
            return;
        }
    }
    mSuggestedWords.push(SuggestedWord(codePoints, codePointCount, score, type,
            indexToPartialCommit, autoCommitFirstWordConfidence));
}

void SuggestionResults::getSortedScores(int *const outScores) const {
    auto copyOfSuggestedWords = mSuggestedWords;
    while (!copyOfSuggestedWords.empty()) {
        const SuggestedWord &suggestedWord = copyOfSuggestedWords.top();
        outScores[copyOfSuggestedWords.size() - 1] = suggestedWord.getScore();
        copyOfSuggestedWords.pop();
    }
}

void SuggestionResults::dumpSuggestions() const {
    AKLOGE("weight of language model vs spatial model: %f", mWeightOfLangModelVsSpatialModel);
    std::vector<SuggestedWord> suggestedWords;
    auto copyOfSuggestedWords = mSuggestedWords;
    while (!copyOfSuggestedWords.empty()) {
        suggestedWords.push_back(copyOfSuggestedWords.top());
        copyOfSuggestedWords.pop();
    }
    int index = 0;
    for (auto it = suggestedWords.rbegin(); it != suggestedWords.rend(); ++it) {
        DUMP_SUGGESTION(it->getCodePoint(), it->getCodePointCount(), index, it->getScore());
        index++;
    }
}

} // namespace latinime
