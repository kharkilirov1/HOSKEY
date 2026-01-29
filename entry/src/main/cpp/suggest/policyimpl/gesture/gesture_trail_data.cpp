/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 *
 * GestureTrailData - Yandex-style gesture trail implementation
 *
 * Port of Yandex ff.k (C3523k) trail data processing
 */

#include "gesture_trail_data.h"
#include <cstring>

namespace latinime {

// ============================================================================
// GestureTrailData Implementation
// ============================================================================

GestureTrailData::GestureTrailData() {
    mPoints.reserve(256);
}

GestureTrailData::~GestureTrailData() {
    reset();
}

void GestureTrailData::reset() {
    mPoints.clear();
    mPointerId = -1;
    mGestureStartTime = 0;
    mFirstVisibleIndex = 0;
    mLastInterpolatedIndex = 0;
    mVisibleCount = 0;
    mLastRawIndex = 0;
}

void GestureTrailData::copyFromRawPoints(
    const int* xPoints,
    const int* yPoints,
    const int64_t* timestamps,
    size_t pointCount,
    int64_t currentTime,
    const TrailParams& params) {

    if (pointCount == 0) return;

    // Set gesture start time from first point
    if (mGestureStartTime == 0 && pointCount > 0) {
        mGestureStartTime = timestamps[0];
    }

    // Process only new points (incremental update)
    size_t startIdx = mLastRawIndex;
    if (startIdx >= pointCount) return;

    // Convert raw points to float and store
    std::vector<float> xFloat(pointCount);
    std::vector<float> yFloat(pointCount);

    for (size_t i = 0; i < pointCount; ++i) {
        xFloat[i] = static_cast<float>(xPoints[i]);
        yFloat[i] = static_cast<float>(yPoints[i]);
    }

    // Setup bezier interpolator for tangent calculation
    mBezier.xPoints = xFloat.data();
    mBezier.yPoints = yFloat.data();
    mBezier.pointCount = pointCount;

    // Process each new segment
    for (size_t i = startIdx; i < pointCount; ++i) {
        float x = xFloat[i];
        float y = yFloat[i];
        int32_t relTime = static_cast<int32_t>(timestamps[i] - mGestureStartTime);

        // For first point or after segment break, just add
        if (mPoints.empty() || (i > 0 && isSegmentBreak(mPoints.size() - 1))) {
            mPoints.emplace_back(x, y, relTime);
            continue;
        }

        // Check distance from last point
        const TrailPoint& lastPoint = mPoints.back();
        float dx = x - lastPoint.x;
        float dy = y - lastPoint.y;
        float distance = std::sqrt(dx * dx + dy * dy);

        // Skip if too close
        if (distance < params.minSamplingDistance) {
            continue;
        }

        // Calculate angle change for interpolation decision
        float angleDiff = 0.0f;
        if (mPoints.size() >= 2) {
            const TrailPoint& prevPrev = mPoints[mPoints.size() - 2];
            float prevDx = lastPoint.x - prevPrev.x;
            float prevDy = lastPoint.y - prevPrev.y;

            // Normalize vectors
            float prevLen = std::sqrt(prevDx * prevDx + prevDy * prevDy);
            float currLen = distance;

            if (prevLen > 0.001f && currLen > 0.001f) {
                prevDx /= prevLen;
                prevDy /= prevLen;
                float currDxNorm = dx / currLen;
                float currDyNorm = dy / currLen;

                // Dot product gives cos(angle)
                float dot = prevDx * currDxNorm + prevDy * currDyNorm;
                dot = std::max(-1.0f, std::min(1.0f, dot));
                angleDiff = std::acos(dot);
            }
        }

        // Calculate interpolation steps
        int steps = calculateSteps(angleDiff, distance, params);

        if (steps > 1 && i >= 2 && i + 1 < pointCount) {
            // Setup bezier segment for interpolation
            size_t prevIdx = (i >= 2) ? i - 2 : 0;
            size_t currIdx = i - 1;
            size_t nextIdx = i;
            size_t nextNextIdx = (i + 1 < pointCount) ? i + 1 : i;

            mBezier.x0 = xFloat[currIdx];
            mBezier.y0 = yFloat[currIdx];
            mBezier.x1 = xFloat[nextIdx];
            mBezier.y1 = yFloat[nextIdx];

            // Calculate tangents using Catmull-Rom style
            calculateTangent(xFloat.data(), yFloat.data(), pointCount,
                prevIdx, currIdx, nextIdx, dx, dy,
                mBezier.tangentX0, mBezier.tangentY0);

            calculateTangent(xFloat.data(), yFloat.data(), pointCount,
                currIdx, nextIdx, nextNextIdx, dx, dy,
                mBezier.tangentX1, mBezier.tangentY1);

            // Interpolate points
            int32_t prevTime = static_cast<int32_t>(timestamps[i - 1] - mGestureStartTime);
            int32_t timeStep = (relTime - prevTime) / steps;

            for (int s = 1; s < steps; ++s) {
                float t = static_cast<float>(s) / static_cast<float>(steps);
                float interpX, interpY;
                mBezier.interpolate(t, interpX, interpY);

                int32_t interpTime = prevTime + timeStep * s;
                mPoints.emplace_back(interpX, interpY, interpTime);
            }
        }

        // Add the actual point
        mPoints.emplace_back(x, y, relTime);
    }

    mLastRawIndex = pointCount;

    // Clear bezier pointer references
    mBezier.xPoints = nullptr;
    mBezier.yPoints = nullptr;
    mBezier.pointCount = 0;
}

void GestureTrailData::calculateTangent(
    const float* xData, const float* yData, size_t count,
    size_t prevIdx, size_t currIdx, size_t nextIdx,
    float dx, float dy,
    float& outTx, float& outTy) {

    // Catmull-Rom tangent calculation (Yandex style)
    // Tangent = 0.5 * (P[i+1] - P[i-1])

    if (prevIdx == currIdx) {
        // Forward difference at start
        outTx = (xData[nextIdx] - xData[currIdx]) * 0.5f;
        outTy = (yData[nextIdx] - yData[currIdx]) * 0.5f;
    } else if (nextIdx == currIdx) {
        // Backward difference at end
        outTx = (xData[currIdx] - xData[prevIdx]) * 0.5f;
        outTy = (yData[currIdx] - yData[prevIdx]) * 0.5f;
    } else {
        // Central difference
        outTx = (xData[nextIdx] - xData[prevIdx]) * 0.5f;
        outTy = (yData[nextIdx] - yData[prevIdx]) * 0.5f;
    }
}

int GestureTrailData::calculateSteps(float angleDiff, float segmentLength, const TrailParams& params) {
    // More steps for sharp angles or long segments (Yandex algorithm)
    int angleSteps = 1;
    int lengthSteps = 1;

    if (angleDiff > params.maxAngleRadians) {
        // Scale steps by angle magnitude
        angleSteps = static_cast<int>(angleDiff / params.maxAngleRadians) + 1;
    }

    if (segmentLength > params.maxSegmentLength) {
        // Scale steps by length
        lengthSteps = static_cast<int>(segmentLength / params.maxSegmentLength) + 1;
    }

    int steps = std::max(angleSteps, lengthSteps);
    return std::min(steps, params.maxInterpolationSteps);
}

int GestureTrailData::updateVisibility(int64_t currentTime, const TrailRenderParams& params) {
    if (mPoints.empty()) {
        mVisibleCount = 0;
        return 0;
    }

    int64_t gestureElapsed = currentTime - mGestureStartTime;

    // Find first visible point (not yet expired)
    size_t firstVisible = mPoints.size();
    for (size_t i = mFirstVisibleIndex; i < mPoints.size(); ++i) {
        int64_t pointAge = gestureElapsed - mPoints[i].relativeTime;
        if (pointAge < params.totalLifetimeMs) {
            firstVisible = i;
            break;
        }
    }

    mFirstVisibleIndex = firstVisible;

    if (firstVisible >= mPoints.size()) {
        mVisibleCount = 0;
        return 0;
    }

    mVisibleCount = mPoints.size() - firstVisible;
    return static_cast<int>(mVisibleCount);
}

int GestureTrailData::getSegmentsForRendering(
    int64_t currentTime,
    const TrailRenderParams& params,
    std::vector<float>& outSegments) {

    outSegments.clear();

    if (mVisibleCount < 2) return 0;

    int64_t gestureElapsed = currentTime - mGestureStartTime;

    // Reserve space: each segment has 7 floats (x0, y0, w0, x1, y1, w1, alpha)
    outSegments.reserve(mVisibleCount * 7);

    for (size_t i = mFirstVisibleIndex; i < mPoints.size() - 1; ++i) {
        // Skip segment breaks
        if (isSegmentBreak(i) || isSegmentBreak(i + 1)) {
            continue;
        }

        const TrailPoint& p0 = mPoints[i];
        const TrailPoint& p1 = mPoints[i + 1];

        // Calculate fade for each point
        auto calcFade = [&](const TrailPoint& pt) -> float {
            int64_t age = gestureElapsed - pt.relativeTime;

            if (age < params.fadeStartTimeMs) {
                return 1.0f; // Fully visible
            }

            if (age >= params.totalLifetimeMs) {
                return 0.0f; // Fully faded
            }

            // Linear fade
            float fadeProgress = static_cast<float>(age - params.fadeStartTimeMs) /
                                static_cast<float>(params.fadeDurationMs);
            return 1.0f - fadeProgress;
        };

        float alpha0 = calcFade(p0);
        float alpha1 = calcFade(p1);

        // Skip fully transparent segments
        if (alpha0 <= 0.0f && alpha1 <= 0.0f) {
            continue;
        }

        // Calculate width based on fade (Yandex tapering)
        float width0 = params.minWidth + (params.maxWidth - params.minWidth) * alpha0;
        float width1 = params.minWidth + (params.maxWidth - params.minWidth) * alpha1;

        width0 *= params.widthRatio;
        width1 *= params.widthRatio;

        // Average alpha for segment
        float segmentAlpha = (alpha0 + alpha1) * 0.5f;

        // Add segment data
        outSegments.push_back(p0.x);
        outSegments.push_back(p0.y);
        outSegments.push_back(width0);
        outSegments.push_back(p1.x);
        outSegments.push_back(p1.y);
        outSegments.push_back(width1);
        outSegments.push_back(segmentAlpha);
    }

    return static_cast<int>(outSegments.size() / 7);
}

void GestureTrailData::compactBuffers() {
    if (mFirstVisibleIndex == 0) return;

    // Erase expired points
    mPoints.erase(mPoints.begin(), mPoints.begin() + mFirstVisibleIndex);

    // Reset indices
    mFirstVisibleIndex = 0;
    mVisibleCount = mPoints.size();
}

bool GestureTrailData::isSegmentBreak(size_t index) const {
    if (index >= mPoints.size()) return false;
    return mPoints[index].relativeTime == SEGMENT_BREAK_MARKER;
}

void GestureTrailData::markSegmentBreak(size_t index) {
    if (index >= mPoints.size()) return;
    mPoints[index].relativeTime = SEGMENT_BREAK_MARKER;
}

void GestureTrailData::insertPoint(size_t index, float x, float y, int32_t time) {
    if (index > mPoints.size()) index = mPoints.size();
    mPoints.insert(mPoints.begin() + index, TrailPoint(x, y, time));
}

// ============================================================================
// GestureTrailDrawer Implementation
// ============================================================================

GestureTrailDrawer::GestureTrailDrawer() {
    mXBuffer.reserve(256);
    mYBuffer.reserve(256);
    mTimeBuffer.reserve(256);
}

GestureTrailDrawer::~GestureTrailDrawer() {
    reset();
}

void GestureTrailDrawer::reset() {
    mXBuffer.clear();
    mYBuffer.clear();
    mTimeBuffer.clear();
    mPointCount = 0;
    mLastRenderIndex = 0;
    mPointerId = -1;
}

void GestureTrailDrawer::addPoint(int x, int y, int64_t timestamp) {
    // Check minimum distance from last point
    if (mPointCount > 0) {
        int lastX = mXBuffer[mPointCount - 1];
        int lastY = mYBuffer[mPointCount - 1];

        float dx = static_cast<float>(x - lastX);
        float dy = static_cast<float>(y - lastY);
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < MIN_POINT_DISTANCE) {
            return; // Too close, skip
        }
    }

    mXBuffer.push_back(x);
    mYBuffer.push_back(y);
    mTimeBuffer.push_back(timestamp);
    mPointCount++;
}

} // namespace latinime
