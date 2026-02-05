/**
 * Test for YandexDict + MindSpore Scorer
 */

#include "yandex_trie.h"
#include "nnrt_scorer.h"  // NNRt + CANNKit (replaces mindspore_scorer.h)
#include <iostream>
#include <chrono>
#include <iomanip>

int main(int argc, char* argv[]) {
    std::string dictPath = "/mnt/c/Users/Kharki/DevEcoStudioProjects/HOSKEY/yandex_bundle/clawd/_root_files/main_ru";
    std::string modelPath = "/mnt/c/Users/Kharki/DevEcoStudioProjects/HOSKEY/yandex_bundle/clawd/mindspore_models/ranker.ms";
    
    if (argc > 1) dictPath = argv[1];
    if (argc > 2) modelPath = argv[2];
    
    std::cout << "=== YandexDict + MindSpore Test ===\n\n";
    
    // Load dictionary
    std::cout << "Loading dictionary: " << dictPath << "\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    yandex::YandexDict dict;
    bool dictLoaded = dict.load(dictPath);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    if (!dictLoaded) {
        std::cerr << "❌ Failed to load dictionary!\n";
        return 1;
    }
    
    std::cout << "✅ Dictionary loaded in " << duration.count() << " ms\n";
    std::cout << "   Words: " << dict.getWordCount() << "\n\n";
    
    // Load MindSpore model
    std::cout << "Loading MindSpore model: " << modelPath << "\n";
    
    yandex::MindSporeScorer scorer;
    bool modelLoaded = scorer.loadModel(modelPath);
    
    if (!modelLoaded) {
        std::cout << "⚠️ Model not loaded (using heuristic scoring)\n\n";
    } else {
        std::cout << "✅ Model loaded\n\n";
    }
    
    // Test suggestions with neural scoring
    std::cout << "=== Testing Neural Suggestions ===\n";
    
    const char* testPrefixes[] = {"при", "здр", "спас", "прив", "хор"};
    
    for (const char* prefix : testPrefixes) {
        std::cout << "\nПрефикс: \"" << prefix << "\"\n";
        
        auto startSuggest = std::chrono::high_resolution_clock::now();
        
        // Get candidates from trie
        auto trieSuggestions = dict.getSuggestions(prefix, 20);
        
        // Prepare for neural scoring
        std::vector<yandex::ScoringCandidate> candidates;
        for (const auto& s : trieSuggestions) {
            yandex::ScoringCandidate c;
            c.word = s.word;
            c.wordId = dict.getWordId(s.word);
            c.baseScore = s.score;
            candidates.push_back(c);
        }
        
        // Score with neural model
        auto scored = scorer.score(candidates, prefix);
        
        auto endSuggest = std::chrono::high_resolution_clock::now();
        auto suggestTime = std::chrono::duration_cast<std::chrono::microseconds>(endSuggest - startSuggest);
        
        std::cout << "  (total time: " << suggestTime.count() << " μs)\n";
        
        // Show top 5
        for (size_t i = 0; i < std::min((size_t)5, scored.size()); i++) {
            const auto& s = scored[i];
            std::cout << "  " << (i+1) << ". " << std::setw(20) << std::left << s.word
                      << " score=" << std::fixed << std::setprecision(3) << s.score
                      << " (freq=" << std::setprecision(2) << s.freqScore
                      << ", neural=" << s.neuralScore << ")\n";
        }
    }
    
    // Interactive test
    std::cout << "\n=== Interactive Test ===\n";
    std::cout << "Enter prefix (or 'quit' to exit):\n";
    
    std::string input;
    while (std::cout << "> " && std::getline(std::cin, input)) {
        if (input == "quit" || input == "q") break;
        if (input.empty()) continue;
        
        auto suggestions = dict.getSuggestions(input, 15);
        
        std::vector<yandex::ScoringCandidate> candidates;
        for (const auto& s : suggestions) {
            yandex::ScoringCandidate c;
            c.word = s.word;
            c.wordId = dict.getWordId(s.word);
            c.baseScore = s.score;
            candidates.push_back(c);
        }
        
        auto scored = scorer.score(candidates, input);
        
        std::cout << "Top 10 suggestions:\n";
        for (size_t i = 0; i < std::min((size_t)10, scored.size()); i++) {
            std::cout << "  " << (i+1) << ". " << scored[i].word 
                      << " (" << std::fixed << std::setprecision(3) << scored[i].score << ")\n";
        }
    }
    
    std::cout << "\n=== Done ===\n";
    return 0;
}
