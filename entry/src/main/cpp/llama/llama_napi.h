/**
 * HOSKEY LLaMA NAPI Bridge
 * 
 * Порт Yandex NativeBridge на HarmonyOS NAPI.
 * Обёртка над llama.cpp для нейрокоррекции текста.
 * 
 * НЕ ПОДКЛЮЧЕНО К ОСНОВНОМУ КОДУ - standalone модуль для тестирования.
 */

#ifndef HOSKEY_LLAMA_NAPI_H
#define HOSKEY_LLAMA_NAPI_H

#include <napi/native_api.h>
#include <string>
#include <memory>
#include <mutex>

namespace hoskey {
namespace llama {

/**
 * Результат completion
 */
struct CompletionResult {
    std::string text;       // Сгенерированный текст
    float probability;      // Минимальная вероятность токена
    bool success;           // Успешно ли завершено
    std::string error;      // Сообщение об ошибке (если есть)
};

/**
 * Конфигурация модели
 */
struct ModelConfig {
    int contextSize = 256;           // Размер контекста в токенах
    bool useKvCacheQuantization = false;  // INT8 для экономии RAM
    float probabilityThreshold = 0.387f;  // Порог уверенности
    int maxResponseMultiplier = 2;   // Макс. ответ = input * multiplier
};

/**
 * LlamaBridge - главный класс для работы с LLaMA
 * 
 * Thread-safe singleton.
 */
class LlamaBridge {
public:
    static LlamaBridge& getInstance();
    
    // Запрет копирования
    LlamaBridge(const LlamaBridge&) = delete;
    LlamaBridge& operator=(const LlamaBridge&) = delete;
    
    /**
     * Инициализация backend (вызвать один раз при старте)
     */
    bool initialize();
    
    /**
     * Загрузить модель из файла
     * @param modelPath путь к .gguf файлу
     * @param config конфигурация
     * @return true если успешно
     */
    bool loadModel(const std::string& modelPath, const ModelConfig& config);
    
    /**
     * Выполнить completion (коррекцию текста)
     * @param inputText входной текст с возможными ошибками
     * @return результат коррекции
     */
    CompletionResult complete(const std::string& inputText);
    
    /**
     * Выгрузить модель и освободить память
     */
    void unload();
    
    /**
     * Проверить, загружена ли модель
     */
    bool isLoaded() const;
    
    /**
     * Получить/установить порог вероятности
     */
    float getProbabilityThreshold() const { return config_.probabilityThreshold; }
    void setProbabilityThreshold(float threshold) { config_.probabilityThreshold = threshold; }

private:
    LlamaBridge();
    ~LlamaBridge();
    
    // Native handles (указатели на llama.cpp структуры)
    void* modelHandle_ = nullptr;
    void* contextHandle_ = nullptr;
    void* batchHandle_ = nullptr;
    void* vocabHandle_ = nullptr;
    void* samplerHandle_ = nullptr;
    
    ModelConfig config_;
    bool initialized_ = false;
    bool loaded_ = false;
    
    mutable std::mutex mutex_;
    
    // Internal methods
    void clearKvCache();
    bool isDone();
};

// ============== NAPI Exports ==============

/**
 * Регистрация NAPI функций
 */
napi_value LlamaModuleInit(napi_env env, napi_value exports);

// NAPI functions
napi_value LlamaInitialize(napi_env env, napi_callback_info info);
napi_value LlamaLoadModel(napi_env env, napi_callback_info info);
napi_value LlamaComplete(napi_env env, napi_callback_info info);
napi_value LlamaUnload(napi_env env, napi_callback_info info);
napi_value LlamaIsLoaded(napi_env env, napi_callback_info info);
napi_value LlamaSetThreshold(napi_env env, napi_callback_info info);

} // namespace llama
} // namespace hoskey

#endif // HOSKEY_LLAMA_NAPI_H
