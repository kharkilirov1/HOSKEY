/*
 * Copyright (c) [2026] [Your Name or Project HOSKEY]
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

#ifndef HOSKEY_NAPI_HELPERS_H
#define HOSKEY_NAPI_HELPERS_H

#include <napi/native_api.h>
#include <cstdint>

namespace hoskey {

/**
 * @brief NAPI эквивалент env->GetIntArrayRegion()
 * Копирует элементы из NAPI-массива в C++ буфер.
 * @param env Контекст NAPI.
 * @param jArray Массив NAPI.
 * @param start Начальный индекс (обычно 0).
 * @param len Количество элементов для копирования.
 * @param buffer Указатель на буфер назначения.
 * @return true в случае успеха, false в случае ошибки.
 */
bool napiGetIntArrayRegion(napi_env env, napi_value jArray,
                           uint32_t start, uint32_t len, int32_t* buffer);

/**
 * @brief NAPI эквивалент env->GetFloatArrayRegion()
 * Копирует элементы из NAPI-массива в C++ буфер.
 * @param env Контекст NAPI.
 * @param jArray Массив NAPI.
 * @param start Начальный индекс (обычно 0).
 * @param len Количество элементов для копирования.
 * @param buffer Указатель на буфер назначения.
 * @return true в случае успеха, false в случае ошибки.
 */
bool napiGetFloatArrayRegion(napi_env env, napi_value jArray,
                             uint32_t start, uint32_t len, float* buffer);

/**
 * @brief NAPI эквивалент env->SetIntArrayRegion()
 * Копирует элементы из C++ буфера в NAPI-массив.
 * @param env Контекст NAPI.
 * @param jArray Массив NAPI.
 * @param start Начальный индекс (обычно 0).
 * @param len Количество элементов для копирования.
 * @param buffer Указатель на исходный буфер.
 * @return true в случае успеха, false в случае ошибки.
 */
bool napiSetIntArrayRegion(napi_env env, napi_value jArray,
                           uint32_t start, uint32_t len, const int32_t* buffer);

/**
 * @brief NAPI эквивалент env->SetFloatArrayRegion()
 * Копирует элементы из C++ буфера в NAPI-массив.
 * @param env Контекст NAPI.
 * @param jArray Массив NAPI.
 * @param start Начальный индекс (обычно 0).
 * @param len Количество элементов для копирования.
 * @param buffer Указатель на исходный буфер.
 * @return true в случае успеха, false в случае ошибки.
 */
bool napiSetFloatArrayRegion(napi_env env, napi_value jArray,
                             uint32_t start, uint32_t len, const float* buffer);

/**
 * @brief Создает NAPI int32 значение.
 * @param env Контекст NAPI.
 * @param value Значение int32.
 * @return NAPI-значение или nullptr в случае ошибки.
 */
napi_value napiCreateInt(napi_env env, int32_t value);

/**
 * @brief Создает NAPI double значение.
 * @param env Контекст NAPI.
 * @param value Значение double.
 * @return NAPI-значение или nullptr в случае ошибки.
 */
napi_value napiCreateDouble(napi_env env, double value);

/**
 * @brief Получает длину массива с проверкой на null/undefined.
 * @param env Контекст NAPI.
 * @param array NAPI массив.
 * @return Длина массива или 0 если массив невалиден.
 */
uint32_t napiGetArrayLengthChecked(napi_env env, napi_value array);

/**
 * @brief Проверяет, является ли значение null или undefined.
 * @param env Контекст NAPI.
 * @param value NAPI значение.
 * @return true если null/undefined, иначе false.
 */
bool napiIsNullOrUndefined(napi_env env, napi_value value);

/**
 * @brief Создает новый NAPI-массив целых чисел из C++ буфера.
 * @param env Контекст NAPI.
 * @param buffer Указатель на исходный буфер.
 * @param len Количество элементов в буфере.
 * @return NAPI-значение нового массива или nullptr в случае ошибки.
 */
napi_value napiCreateIntArray(napi_env env, const int32_t* buffer, uint32_t len);

/**
 * @brief Создает новый NAPI-массив чисел с плавающей точкой из C++ буфера.
 * @param env Контекст NAPI.
 * @param buffer Указатель на исходный буфер.
 * @param len Количество элементов в буфере.
 * @return NAPI-значение нового массива или nullptr в случае ошибки.
 */
napi_value napiCreateFloatArray(napi_env env, const float* buffer, uint32_t len);

} // namespace hoskey

#endif // HOSKEY_NAPI_HELPERS_H