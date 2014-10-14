/*
 * Copyright (C) 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * !!!!! DO NOT CHANGE THE LOGIC IN THIS FILE !!!!!
 * Do not edit this file other than updating policy's interface.
 *
 * This file was generated from
 *   suggest/policyimpl/dictionary/structure/v4/ver4_patricia_trie_policy.cpp
 */

#include "suggest/policyimpl/dictionary/structure/backward/v402/ver4_patricia_trie_policy.h"

#include <vector>

#include "suggest/core/dicnode/dic_node.h"
#include "suggest/core/dicnode/dic_node_vector.h"
#include "suggest/core/dictionary/multi_bigram_map.h"
#include "suggest/core/dictionary/ngram_listener.h"
#include "suggest/core/dictionary/property/ngram_property.h"
#include "suggest/core/dictionary/property/unigram_property.h"
#include "suggest/core/dictionary/property/word_property.h"
#include "suggest/core/session/ngram_context.h"
#include "suggest/policyimpl/dictionary/structure/pt_common/dynamic_pt_reading_helper.h"
#include "suggest/policyimpl/dictionary/structure/backward/v402/ver4_patricia_trie_node_reader.h"
#include "suggest/policyimpl/dictionary/utils/forgetting_curve_utils.h"
#include "suggest/policyimpl/dictionary/utils/probability_utils.h"

namespace latinime {
namespace backward {
namespace v402 {

// Note that there are corresponding definitions in Java side in BinaryDictionaryTests and
// BinaryDictionaryDecayingTests.
const char *const Ver4PatriciaTriePolicy::UNIGRAM_COUNT_QUERY = "UNIGRAM_COUNT";
const char *const Ver4PatriciaTriePolicy::BIGRAM_COUNT_QUERY = "BIGRAM_COUNT";
const char *const Ver4PatriciaTriePolicy::MAX_UNIGRAM_COUNT_QUERY = "MAX_UNIGRAM_COUNT";
const char *const Ver4PatriciaTriePolicy::MAX_BIGRAM_COUNT_QUERY = "MAX_BIGRAM_COUNT";
const int Ver4PatriciaTriePolicy::MARGIN_TO_REFUSE_DYNAMIC_OPERATIONS = 1024;
const int Ver4PatriciaTriePolicy::MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS =
        Ver4DictConstants::MAX_DICTIONARY_SIZE - MARGIN_TO_REFUSE_DYNAMIC_OPERATIONS;
const int Ver4PatriciaTriePolicy::DUMMY_PROBABILITY_FOR_VALID_WORDS = 1;

void Ver4PatriciaTriePolicy::createAndGetAllChildDicNodes(const DicNode *const dicNode,
        DicNodeVector *const childDicNodes) const {
    if (!dicNode->hasChildren()) {
        return;
    }
    DynamicPtReadingHelper readingHelper(&mNodeReader, &mPtNodeArrayReader);
    readingHelper.initWithPtNodeArrayPos(dicNode->getChildrenPtNodeArrayPos());
    while (!readingHelper.isEnd()) {
        const PtNodeParams ptNodeParams = readingHelper.getPtNodeParams();
        if (!ptNodeParams.isValid()) {
            break;
        }
        bool isTerminal = ptNodeParams.isTerminal() && !ptNodeParams.isDeleted();
        if (isTerminal && mHeaderPolicy->isDecayingDict()) {
            // A DecayingDict may have a terminal PtNode that has a terminal DicNode whose
            // probability is NOT_A_PROBABILITY. In such case, we don't want to treat it as a
            // valid terminal DicNode.
            isTerminal = ptNodeParams.getProbability() != NOT_A_PROBABILITY;
        }
        readingHelper.readNextSiblingNode(ptNodeParams);
        if (ptNodeParams.representsNonWordInfo()) {
            // Skip PtNodes that represent non-word information.
            continue;
        }
        const int wordId = isTerminal ? ptNodeParams.getHeadPos() : NOT_A_WORD_ID;
        childDicNodes->pushLeavingChild(dicNode, ptNodeParams.getChildrenPos(),
                wordId, ptNodeParams.getCodePointArrayView());
    }
    if (readingHelper.isError()) {
        mIsCorrupted = true;
        AKLOGE("Dictionary reading error in createAndGetAllChildDicNodes().");
    }
}

int Ver4PatriciaTriePolicy::getCodePointsAndReturnCodePointCount(const int wordId,
        const int maxCodePointCount, int *const outCodePoints) const {
    DynamicPtReadingHelper readingHelper(&mNodeReader, &mPtNodeArrayReader);
    const int ptNodePos = getTerminalPtNodePosFromWordId(wordId);
    readingHelper.initWithPtNodePos(ptNodePos);
    const int codePointCount =  readingHelper.getCodePointsAndReturnCodePointCount(
            maxCodePointCount, outCodePoints);
    if (readingHelper.isError()) {
        mIsCorrupted = true;
        AKLOGE("Dictionary reading error in getCodePointsAndProbabilityAndReturnCodePointCount().");
    }
    return codePointCount;
}

int Ver4PatriciaTriePolicy::getWordId(const CodePointArrayView wordCodePoints,
        const bool forceLowerCaseSearch) const {
    DynamicPtReadingHelper readingHelper(&mNodeReader, &mPtNodeArrayReader);
    readingHelper.initWithPtNodeArrayPos(getRootPosition());
    const int ptNodePos = readingHelper.getTerminalPtNodePositionOfWord(wordCodePoints.data(),
            wordCodePoints.size(), forceLowerCaseSearch);
    if (readingHelper.isError()) {
        mIsCorrupted = true;
        AKLOGE("Dictionary reading error in getWordId().");
    }
    return getWordIdFromTerminalPtNodePos(ptNodePos);
}

const WordAttributes Ver4PatriciaTriePolicy::getWordAttributesInContext(
        const WordIdArrayView prevWordIds, const int wordId,
        MultiBigramMap *const multiBigramMap) const {
    if (wordId == NOT_A_WORD_ID) {
        return WordAttributes();
    }
    const int ptNodePos = getTerminalPtNodePosFromWordId(wordId);
    const PtNodeParams ptNodeParams(mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(ptNodePos));
    if (multiBigramMap) {
        const int probability = multiBigramMap->getBigramProbability(this /* structurePolicy */,
                prevWordIds, wordId, ptNodeParams.getProbability());
        return getWordAttributes(probability, ptNodeParams);
    }
    if (!prevWordIds.empty()) {
        const int probability = getProbabilityOfWord(prevWordIds, wordId);
        if (probability != NOT_A_PROBABILITY) {
            return getWordAttributes(probability, ptNodeParams);
        }
    }
    return getWordAttributes(getProbability(ptNodeParams.getProbability(), NOT_A_PROBABILITY),
            ptNodeParams);
}

const WordAttributes Ver4PatriciaTriePolicy::getWordAttributes(const int probability,
        const PtNodeParams &ptNodeParams) const {
    return WordAttributes(probability, false /* isBlacklisted */, ptNodeParams.isNotAWord(),
            ptNodeParams.getProbability() == 0);
}

int Ver4PatriciaTriePolicy::getProbability(const int unigramProbability,
        const int bigramProbability) const {
    // In the v4 format, bigramProbability is a conditional probability.
    const int bigramConditionalProbability = bigramProbability;
    if (unigramProbability == NOT_A_PROBABILITY) {
        return NOT_A_PROBABILITY;
    }
    if (bigramConditionalProbability == NOT_A_PROBABILITY) {
        return ProbabilityUtils::backoff(unigramProbability);
    }
    return bigramConditionalProbability;
}

int Ver4PatriciaTriePolicy::getProbabilityOfWord(const WordIdArrayView prevWordIds,
        const int wordId) const {
    if (wordId == NOT_A_WORD_ID) {
        return NOT_A_PROBABILITY;
    }
    const int ptNodePos = getTerminalPtNodePosFromWordId(wordId);
    const PtNodeParams ptNodeParams(mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(ptNodePos));
    if (ptNodeParams.isDeleted() || ptNodeParams.isNotAWord()) {
        return NOT_A_PROBABILITY;
    }
    if (prevWordIds.empty()) {
        return getProbability(ptNodeParams.getProbability(), NOT_A_PROBABILITY);
    }
    if (prevWordIds[0] == NOT_A_WORD_ID) {
        return NOT_A_PROBABILITY;
    }
    const PtNodeParams prevWordPtNodeParams =
            mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(prevWordIds[0]);
    if (prevWordPtNodeParams.isDeleted()) {
        return getProbability(ptNodeParams.getProbability(), NOT_A_PROBABILITY);
    }
    const int bigramsPosition = mBuffers->getBigramDictContent()->getBigramListHeadPos(
            prevWordPtNodeParams.getTerminalId());
    BinaryDictionaryBigramsIterator bigramsIt(&mBigramPolicy, bigramsPosition);
    while (bigramsIt.hasNext()) {
        bigramsIt.next();
        if (bigramsIt.getBigramPos() == ptNodePos
                && bigramsIt.getProbability() != NOT_A_PROBABILITY) {
            const int bigramConditionalProbability = getBigramConditionalProbability(
                    prevWordPtNodeParams.getProbability(),
                    prevWordPtNodeParams.representsBeginningOfSentence(),
                    bigramsIt.getProbability());
            return getProbability(ptNodeParams.getProbability(), bigramConditionalProbability);
        }
    }
    return NOT_A_PROBABILITY;
}

void Ver4PatriciaTriePolicy::iterateNgramEntries(const WordIdArrayView prevWordIds,
        NgramListener *const listener) const {
    if (prevWordIds.firstOrDefault(NOT_A_DICT_POS) == NOT_A_DICT_POS) {
        return;
    }
    const PtNodeParams prevWordPtNodeParams =
            mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(prevWordIds[0]);
    if (prevWordPtNodeParams.isDeleted()) {
        return;
    }
    const int bigramsPosition = mBuffers->getBigramDictContent()->getBigramListHeadPos(
            prevWordPtNodeParams.getTerminalId());
    BinaryDictionaryBigramsIterator bigramsIt(&mBigramPolicy, bigramsPosition);
    while (bigramsIt.hasNext()) {
        bigramsIt.next();
        const int bigramConditionalProbability = getBigramConditionalProbability(
                prevWordPtNodeParams.getProbability(),
                prevWordPtNodeParams.representsBeginningOfSentence(), bigramsIt.getProbability());
        listener->onVisitEntry(bigramConditionalProbability,
                getWordIdFromTerminalPtNodePos(bigramsIt.getBigramPos()));
    }
}

int Ver4PatriciaTriePolicy::getBigramConditionalProbability(const int prevWordUnigramProbability,
        const bool isInBeginningOfSentenceContext, const int bigramProbability) const {
    if (mHeaderPolicy->hasHistoricalInfoOfWords()) {
        if (isInBeginningOfSentenceContext) {
            return bigramProbability;
        }
        // Calculate conditional probability.
        return std::min(MAX_PROBABILITY - prevWordUnigramProbability + bigramProbability,
                MAX_PROBABILITY);
    } else {
        // bigramProbability is a conditional probability.
        return bigramProbability;
    }
}

BinaryDictionaryShortcutIterator Ver4PatriciaTriePolicy::getShortcutIterator(
        const int wordId) const {
    const int shortcutPos = getShortcutPositionOfPtNode(getTerminalPtNodePosFromWordId(wordId));
    return BinaryDictionaryShortcutIterator(&mShortcutPolicy, shortcutPos);
}

int Ver4PatriciaTriePolicy::getShortcutPositionOfPtNode(const int ptNodePos) const {
    if (ptNodePos == NOT_A_DICT_POS) {
        return NOT_A_DICT_POS;
    }
    const PtNodeParams ptNodeParams(mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(ptNodePos));
    if (ptNodeParams.isDeleted()) {
        return NOT_A_DICT_POS;
    }
    return mBuffers->getShortcutDictContent()->getShortcutListHeadPos(
            ptNodeParams.getTerminalId());
}

int Ver4PatriciaTriePolicy::getBigramsPositionOfPtNode(const int ptNodePos) const {
    if (ptNodePos == NOT_A_DICT_POS) {
        return NOT_A_DICT_POS;
    }
    const PtNodeParams ptNodeParams(mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(ptNodePos));
    if (ptNodeParams.isDeleted()) {
        return NOT_A_DICT_POS;
    }
    return mBuffers->getBigramDictContent()->getBigramListHeadPos(
            ptNodeParams.getTerminalId());
}

bool Ver4PatriciaTriePolicy::addUnigramEntry(const CodePointArrayView wordCodePoints,
        const UnigramProperty *const unigramProperty) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: addUnigramEntry() is called for non-updatable dictionary.");
        return false;
    }
    if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS) {
        AKLOGE("The dictionary is too large to dynamically update. Dictionary size: %d",
                mDictBuffer->getTailPosition());
        return false;
    }
    if (wordCodePoints.size() > MAX_WORD_LENGTH) {
        AKLOGE("The word is too long to insert to the dictionary, length: %zd",
                wordCodePoints.size());
        return false;
    }
    for (const auto &shortcut : unigramProperty->getShortcuts()) {
        if (shortcut.getTargetCodePoints()->size() > MAX_WORD_LENGTH) {
            AKLOGE("One of shortcut targets is too long to insert to the dictionary, length: %zd",
                    shortcut.getTargetCodePoints()->size());
            return false;
        }
    }
    DynamicPtReadingHelper readingHelper(&mNodeReader, &mPtNodeArrayReader);
    readingHelper.initWithPtNodeArrayPos(getRootPosition());
    bool addedNewUnigram = false;
    int codePointsToAdd[MAX_WORD_LENGTH];
    int codePointCountToAdd = wordCodePoints.size();
    memmove(codePointsToAdd, wordCodePoints.data(), sizeof(int) * codePointCountToAdd);
    if (unigramProperty->representsBeginningOfSentence()) {
        codePointCountToAdd = CharUtils::attachBeginningOfSentenceMarker(codePointsToAdd,
                codePointCountToAdd, MAX_WORD_LENGTH);
    }
    if (codePointCountToAdd <= 0) {
        return false;
    }
    const CodePointArrayView codePointArrayView(codePointsToAdd, codePointCountToAdd);
    if (mUpdatingHelper.addUnigramWord(&readingHelper, codePointArrayView, unigramProperty,
            &addedNewUnigram)) {
        if (addedNewUnigram && !unigramProperty->representsBeginningOfSentence()) {
            mEntryCounters.incrementUnigramCount();
        }
        if (unigramProperty->getShortcuts().size() > 0) {
            // Add shortcut target.
            const int wordPos = getTerminalPtNodePosFromWordId(
                    getWordId(codePointArrayView, false /* forceLowerCaseSearch */));
            if (wordPos == NOT_A_DICT_POS) {
                AKLOGE("Cannot find terminal PtNode position to add shortcut target.");
                return false;
            }
            for (const auto &shortcut : unigramProperty->getShortcuts()) {
                if (!mUpdatingHelper.addShortcutTarget(wordPos,
                        CodePointArrayView(*shortcut.getTargetCodePoints()),
                        shortcut.getProbability())) {
                    AKLOGE("Cannot add new shortcut target. PtNodePos: %d, length: %zd, "
                            "probability: %d", wordPos, shortcut.getTargetCodePoints()->size(),
                            shortcut.getProbability());
                    return false;
                }
            }
        }
        return true;
    } else {
        return false;
    }
}

bool Ver4PatriciaTriePolicy::removeUnigramEntry(const CodePointArrayView wordCodePoints) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: removeUnigramEntry() is called for non-updatable dictionary.");
        return false;
    }
    const int ptNodePos = getTerminalPtNodePosFromWordId(
            getWordId(wordCodePoints, false /* forceLowerCaseSearch */));
    if (ptNodePos == NOT_A_DICT_POS) {
        return false;
    }
    const PtNodeParams ptNodeParams = mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(ptNodePos);
    return mNodeWriter.suppressUnigramEntry(&ptNodeParams);
}

bool Ver4PatriciaTriePolicy::addNgramEntry(const NgramProperty *const ngramProperty) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: addNgramEntry() is called for non-updatable dictionary.");
        return false;
    }
    if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS) {
        AKLOGE("The dictionary is too large to dynamically update. Dictionary size: %d",
                mDictBuffer->getTailPosition());
        return false;
    }
    const NgramContext *const ngramContext = ngramProperty->getNgramContext();
    if (!ngramContext->isValid()) {
        AKLOGE("Ngram context is not valid for adding n-gram entry to the dictionary.");
        return false;
    }
    if (ngramProperty->getTargetCodePoints()->size() > MAX_WORD_LENGTH) {
        AKLOGE("The word is too long to insert the ngram to the dictionary. "
                "length: %zd", ngramProperty->getTargetCodePoints()->size());
        return false;
    }
    WordIdArray<MAX_PREV_WORD_COUNT_FOR_N_GRAM> prevWordIdArray;
    const WordIdArrayView prevWordIds = ngramContext->getPrevWordIds(this, &prevWordIdArray,
            false /* tryLowerCaseSearch */);
    if (prevWordIds.empty()) {
        return false;
    }
    if (prevWordIds[0] == NOT_A_WORD_ID) {
        if (ngramContext->isNthPrevWordBeginningOfSentence(1 /* n */)) {
            const UnigramProperty beginningOfSentenceUnigramProperty(
                    true /* representsBeginningOfSentence */, true /* isNotAWord */,
                    false /* isBlacklisted */, MAX_PROBABILITY /* probability */, HistoricalInfo());
            if (!addUnigramEntry(ngramContext->getNthPrevWordCodePoints(1 /* n */),
                    &beginningOfSentenceUnigramProperty)) {
                AKLOGE("Cannot add unigram entry for the beginning-of-sentence.");
                return false;
            }
            // Refresh word ids.
            ngramContext->getPrevWordIds(this, &prevWordIdArray, false /* tryLowerCaseSearch */);
        } else {
            return false;
        }
    }
    const int wordPos = getTerminalPtNodePosFromWordId(getWordId(
            CodePointArrayView(*ngramProperty->getTargetCodePoints()),
                    false /* forceLowerCaseSearch */));
    if (wordPos == NOT_A_DICT_POS) {
        return false;
    }
    bool addedNewBigram = false;
    const int prevWordPtNodePos = getTerminalPtNodePosFromWordId(prevWordIds[0]);
    if (mUpdatingHelper.addNgramEntry(PtNodePosArrayView::singleElementView(&prevWordPtNodePos),
            wordPos, ngramProperty, &addedNewBigram)) {
        if (addedNewBigram) {
            mEntryCounters.incrementBigramCount();
        }
        return true;
    } else {
        return false;
    }
}

bool Ver4PatriciaTriePolicy::removeNgramEntry(const NgramContext *const ngramContext,
        const CodePointArrayView wordCodePoints) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: removeNgramEntry() is called for non-updatable dictionary.");
        return false;
    }
    if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS) {
        AKLOGE("The dictionary is too large to dynamically update. Dictionary size: %d",
                mDictBuffer->getTailPosition());
        return false;
    }
    if (!ngramContext->isValid()) {
        AKLOGE("Ngram context is not valid for removing n-gram entry form the dictionary.");
        return false;
    }
    if (wordCodePoints.size() > MAX_WORD_LENGTH) {
        AKLOGE("word is too long to remove n-gram entry form the dictionary. length: %zd",
                wordCodePoints.size());
    }
    WordIdArray<MAX_PREV_WORD_COUNT_FOR_N_GRAM> prevWordIdArray;
    const WordIdArrayView prevWordIds = ngramContext->getPrevWordIds(this, &prevWordIdArray,
            false /* tryLowerCaseSerch */);
    if (prevWordIds.firstOrDefault(NOT_A_WORD_ID) == NOT_A_WORD_ID) {
        return false;
    }
    const int wordPos = getTerminalPtNodePosFromWordId(getWordId(wordCodePoints,
            false /* forceLowerCaseSearch */));
    if (wordPos == NOT_A_DICT_POS) {
        return false;
    }
    const int prevWordPtNodePos = getTerminalPtNodePosFromWordId(prevWordIds[0]);
    if (mUpdatingHelper.removeNgramEntry(
            PtNodePosArrayView::singleElementView(&prevWordPtNodePos), wordPos)) {
        mEntryCounters.decrementBigramCount();
        return true;
    } else {
        return false;
    }
}


bool Ver4PatriciaTriePolicy::updateEntriesForWordWithNgramContext(
        const NgramContext *const ngramContext, const CodePointArrayView wordCodePoints,
        const bool isValidWord, const HistoricalInfo historicalInfo) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: updateEntriesForWordWithNgramContext() is called for non-updatable "
                "dictionary.");
        return false;
    }
    const int probability = isValidWord ? DUMMY_PROBABILITY_FOR_VALID_WORDS : NOT_A_PROBABILITY;
    const UnigramProperty unigramProperty(false /* representsBeginningOfSentence */,
            false /* isNotAWord */, false /*isBlacklisted*/, probability, historicalInfo);
    if (!addUnigramEntry(wordCodePoints, &unigramProperty)) {
        AKLOGE("Cannot update unigarm entry in updateEntriesForWordWithNgramContext().");
        return false;
    }
    const int probabilityForNgram = ngramContext->isNthPrevWordBeginningOfSentence(1 /* n */)
            ? NOT_A_PROBABILITY : probability;
    const NgramProperty ngramProperty(*ngramContext, wordCodePoints.toVector(), probabilityForNgram,
            historicalInfo);
    if (!addNgramEntry(&ngramProperty)) {
        AKLOGE("Cannot update unigarm entry in updateEntriesForWordWithNgramContext().");
        return false;
    }
    return true;
}

bool Ver4PatriciaTriePolicy::flush(const char *const filePath) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: flush() is called for non-updatable dictionary. filePath: %s", filePath);
        return false;
    }
    if (!mWritingHelper.writeToDictFile(filePath, mEntryCounters.getEntryCounts())) {
        AKLOGE("Cannot flush the dictionary to file.");
        mIsCorrupted = true;
        return false;
    }
    return true;
}

bool Ver4PatriciaTriePolicy::flushWithGC(const char *const filePath) {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: flushWithGC() is called for non-updatable dictionary.");
        return false;
    }
    if (!mWritingHelper.writeToDictFileWithGC(getRootPosition(), filePath)) {
        AKLOGE("Cannot flush the dictionary to file with GC.");
        mIsCorrupted = true;
        return false;
    }
    return true;
}

bool Ver4PatriciaTriePolicy::needsToRunGC(const bool mindsBlockByGC) const {
    if (!mBuffers->isUpdatable()) {
        AKLOGI("Warning: needsToRunGC() is called for non-updatable dictionary.");
        return false;
    }
    if (mBuffers->isNearSizeLimit()) {
        // Additional buffer size is near the limit.
        return true;
    } else if (mHeaderPolicy->getExtendedRegionSize() + mDictBuffer->getUsedAdditionalBufferSize()
            > Ver4DictConstants::MAX_DICT_EXTENDED_REGION_SIZE) {
        // Total extended region size of the trie exceeds the limit.
        return true;
    } else if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS
            && mDictBuffer->getUsedAdditionalBufferSize() > 0) {
        // Needs to reduce dictionary size.
        return true;
    } else if (mHeaderPolicy->isDecayingDict()) {
        return ForgettingCurveUtils::needsToDecay(mindsBlockByGC, mEntryCounters.getEntryCounts(),
                mHeaderPolicy);
    }
    return false;
}

void Ver4PatriciaTriePolicy::getProperty(const char *const query, const int queryLength,
        char *const outResult, const int maxResultLength) {
    const int compareLength = queryLength + 1 /* terminator */;
    if (strncmp(query, UNIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d", mEntryCounters.getUnigramCount());
    } else if (strncmp(query, BIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d", mEntryCounters.getBigramCount());
    } else if (strncmp(query, MAX_UNIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d",
                mHeaderPolicy->isDecayingDict() ?
                        ForgettingCurveUtils::getEntryCountHardLimit(
                                mHeaderPolicy->getMaxUnigramCount()) :
                        static_cast<int>(Ver4DictConstants::MAX_DICTIONARY_SIZE));
    } else if (strncmp(query, MAX_BIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d",
                mHeaderPolicy->isDecayingDict() ?
                        ForgettingCurveUtils::getEntryCountHardLimit(
                                mHeaderPolicy->getMaxBigramCount()) :
                        static_cast<int>(Ver4DictConstants::MAX_DICTIONARY_SIZE));
    }
}

const WordProperty Ver4PatriciaTriePolicy::getWordProperty(
        const CodePointArrayView wordCodePoints) const {
    const int ptNodePos = getTerminalPtNodePosFromWordId(
            getWordId(wordCodePoints, false /* forceLowerCaseSearch */));
    if (ptNodePos == NOT_A_DICT_POS) {
        AKLOGE("getWordProperty is called for invalid word.");
        return WordProperty();
    }
    const PtNodeParams ptNodeParams = mNodeReader.fetchPtNodeParamsInBufferFromPtNodePos(ptNodePos);
    const ProbabilityEntry probabilityEntry =
            mBuffers->getProbabilityDictContent()->getProbabilityEntry(
                    ptNodeParams.getTerminalId());
    const HistoricalInfo *const historicalInfo = probabilityEntry.getHistoricalInfo();
    // Fetch bigram information.
    std::vector<NgramProperty> ngrams;
    const int bigramListPos = getBigramsPositionOfPtNode(ptNodePos);
    if (bigramListPos != NOT_A_DICT_POS) {
        int bigramWord1CodePoints[MAX_WORD_LENGTH];
        const BigramDictContent *const bigramDictContent = mBuffers->getBigramDictContent();
        const TerminalPositionLookupTable *const terminalPositionLookupTable =
                mBuffers->getTerminalPositionLookupTable();
        bool hasNext = true;
        int readingPos = bigramListPos;
        while (hasNext) {
            const BigramEntry bigramEntry =
                    bigramDictContent->getBigramEntryAndAdvancePosition(&readingPos);
            hasNext = bigramEntry.hasNext();
            const int word1TerminalId = bigramEntry.getTargetTerminalId();
            const int word1TerminalPtNodePos =
                    terminalPositionLookupTable->getTerminalPtNodePosition(word1TerminalId);
            if (word1TerminalPtNodePos == NOT_A_DICT_POS) {
                continue;
            }
            const int codePointCount = getCodePointsAndReturnCodePointCount(
                    getWordIdFromTerminalPtNodePos(word1TerminalPtNodePos), MAX_WORD_LENGTH,
                    bigramWord1CodePoints);
            const HistoricalInfo *const historicalInfo = bigramEntry.getHistoricalInfo();
            const int rawBigramProbability = bigramEntry.hasHistoricalInfo()
                    ? ForgettingCurveUtils::decodeProbability(
                            bigramEntry.getHistoricalInfo(), mHeaderPolicy)
                    : bigramEntry.getProbability();
            const int probability = getBigramConditionalProbability(ptNodeParams.getProbability(),
                    ptNodeParams.representsBeginningOfSentence(), rawBigramProbability);
            ngrams.emplace_back(
                    NgramContext(wordCodePoints.data(), wordCodePoints.size(),
                            ptNodeParams.representsBeginningOfSentence()),
                    CodePointArrayView(bigramWord1CodePoints, codePointCount).toVector(),
                    probability, *historicalInfo);
        }
    }
    // Fetch shortcut information.
    std::vector<UnigramProperty::ShortcutProperty> shortcuts;
    int shortcutPos = getShortcutPositionOfPtNode(ptNodePos);
    if (shortcutPos != NOT_A_DICT_POS) {
        int shortcutTarget[MAX_WORD_LENGTH];
        const ShortcutDictContent *const shortcutDictContent =
                mBuffers->getShortcutDictContent();
        bool hasNext = true;
        while (hasNext) {
            int shortcutTargetLength = 0;
            int shortcutProbability = NOT_A_PROBABILITY;
            shortcutDictContent->getShortcutEntryAndAdvancePosition(MAX_WORD_LENGTH, shortcutTarget,
                    &shortcutTargetLength, &shortcutProbability, &hasNext, &shortcutPos);
            shortcuts.emplace_back(
                    CodePointArrayView(shortcutTarget, shortcutTargetLength).toVector(),
                    shortcutProbability);
        }
    }
    const UnigramProperty unigramProperty(ptNodeParams.representsBeginningOfSentence(),
            ptNodeParams.isNotAWord(), ptNodeParams.isPossiblyOffensive(),
            ptNodeParams.getProbability(), *historicalInfo, std::move(shortcuts));
    return WordProperty(wordCodePoints.toVector(), &unigramProperty, &ngrams);
}

int Ver4PatriciaTriePolicy::getNextWordAndNextToken(const int token, int *const outCodePoints,
        int *const outCodePointCount) {
    *outCodePointCount = 0;
    if (token == 0) {
        mTerminalPtNodePositionsForIteratingWords.clear();
        DynamicPtReadingHelper::TraversePolicyToGetAllTerminalPtNodePositions traversePolicy(
                &mTerminalPtNodePositionsForIteratingWords);
        DynamicPtReadingHelper readingHelper(&mNodeReader, &mPtNodeArrayReader);
        readingHelper.initWithPtNodeArrayPos(getRootPosition());
        readingHelper.traverseAllPtNodesInPostorderDepthFirstManner(&traversePolicy);
    }
    const int terminalPtNodePositionsVectorSize =
            static_cast<int>(mTerminalPtNodePositionsForIteratingWords.size());
    if (token < 0 || token >= terminalPtNodePositionsVectorSize) {
        AKLOGE("Given token %d is invalid.", token);
        return 0;
    }
    const int terminalPtNodePos = mTerminalPtNodePositionsForIteratingWords[token];
    *outCodePointCount = getCodePointsAndReturnCodePointCount(
            getWordIdFromTerminalPtNodePos(terminalPtNodePos), MAX_WORD_LENGTH, outCodePoints);
    const int nextToken = token + 1;
    if (nextToken >= terminalPtNodePositionsVectorSize) {
        // All words have been iterated.
        mTerminalPtNodePositionsForIteratingWords.clear();
        return 0;
    }
    return nextToken;
}

int Ver4PatriciaTriePolicy::getWordIdFromTerminalPtNodePos(const int ptNodePos) const {
    return ptNodePos == NOT_A_DICT_POS ? NOT_A_WORD_ID : ptNodePos;
}

int Ver4PatriciaTriePolicy::getTerminalPtNodePosFromWordId(const int wordId) const {
    return wordId == NOT_A_WORD_ID ? NOT_A_DICT_POS : wordId;
}

} // namespace v402
} // namespace backward
} // namespace latinime
