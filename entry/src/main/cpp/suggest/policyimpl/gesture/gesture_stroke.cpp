/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "gesture_stroke.h"
#include <algorithm>
#include <cmath>

namespace latinime {

// ==================== IntArrayBuffer ====================

IntArrayBuffer::IntArrayBuffer(size_t initialCapacity) : mSize(0) {
    mData.reserve(initialCapacity);
}

IntArrayBuffer::~IntArrayBuffer() = default;

void IntArrayBuffer::add(int value) {
    if (mSize >= mData.size()) {
        mData.push_back(value);
    } else {
        mData[mSize] = value;
    }
    ++mSize;
}

int IntArrayBuffer::get(size_t index) const {
    return index < mSize ? mData[index] : 0;
}

void IntArrayBuffer::set(size_t index, int value) {
    if (index < mSize) {
        mData[index] = value;
    }
}

void IntArrayBuffer::clear() {
    mSize = 0;
}

void IntArrayBuffer::reserve(size_t capacity) {
    mData.reserve(capacity);
}

void IntArrayBuffer::copyTo(int* dest, size_t start, size_t count) const {
    if (!dest || start >= mSize) return;
    size_t actualCount = std::min(count, mSize - start);
    std::copy(mData.begin() + start, mData.begin() + start + actualCount, dest);
}

// ==================== LongArrayBuffer ====================

LongArrayBuffer::LongArrayBuffer(size_t initialCapacity) : mSize(0) {
    mData.reserve(initialCapacity);
}

LongArrayBuffer::~LongArrayBuffer() = default;

void LongArrayBuffer::add(int64_t value) {
    if (mSize >= mData.size()) {
        mData.push_back(value);
    } else {
        mData[mSize] = value;
    }
    ++mSize;
}

int64_t LongArrayBuffer::get(size_t index) const {
    return index < mSize ? mData[index] : 0;
}

void LongArrayBuffer::set(size_t index, int64_t value) {
    if (index < mSize) {
        mData[index] = value;
    }
}

void LongArrayBuffer::clear() {
    mSize = 0;
}

void LongArrayBuffer::reserve(size_t capacity) {
    mData.reserve(capacity);
}

// ==================== GestureStroke ====================

GestureStroke::GestureStroke() 
    : mDistancesDirty(true), mMinX(0), mMinY(0), mMaxX(0), mMaxY(0), mBoundingBoxDirty(true) {
}

GestureStroke::GestureStroke(const GestureParams& params)
    : mDistancesDirty(true), mMinX(0), mMinY(0), mMaxX(0), mMaxY(0), 
      mBoundingBoxDirty(true), mParams(params) {
}

GestureStroke::~GestureStroke() = default;

bool GestureStroke::addPoint(int x, int y, int64_t time) {
    // Check for duplicate point
    if (isDuplicatePoint(x, y)) {
        return false;
    }
    
    // Check for time gap (reset if too long)
    if (mTimestamps.size() > 0) {
        int64_t lastTime = mTimestamps.get(mTimestamps.size() - 1);
        if (time - lastTime > mParams.maxPointInterval) {
            reset();
        }
    }
    
    mXPoints.add(x);
    mYPoints.add(y);
    mTimestamps.add(time);
    
    mDistancesDirty = true;
    mBoundingBoxDirty = true;
    
    return true;
}

void GestureStroke::reset() {
    mXPoints.clear();
    mYPoints.clear();
    mTimestamps.clear();
    mCumulativeDistances.clear();
    mDistancesDirty = true;
    mBoundingBoxDirty = true;
    mMinX = mMinY = mMaxX = mMaxY = 0;
}

bool GestureStroke::hasEnoughPoints() const {
    return mXPoints.size() >= static_cast<size_t>(mParams.minSamplingLength);
}

bool GestureStroke::shouldActivateGestureMode() const {
    // Need at least 2 points to calculate speed
    if (mXPoints.size() < 2) {
        return false;
    }
    
    // Check total distance
    float totalDist = getTotalDistance();
    if (totalDist < mParams.minGestureLength) {
        return false;
    }
    
    // Check if we had a fast initial move
    if (mXPoints.size() >= 2) {
        float speed = getSpeed(1);
        if (speed >= mParams.speedThreshold) {
            return true;
        }
    }
    
    // Check average speed
    float avgSpeed = getAverageSpeed();
    return avgSpeed >= mParams.speedThreshold * mParams.detectionThreshold;
}

float GestureStroke::getSpeed(size_t index) const {
    if (index == 0 || index >= mXPoints.size()) {
        return 0.0f;
    }
    
    int x1 = mXPoints.get(index - 1);
    int y1 = mYPoints.get(index - 1);
    int x2 = mXPoints.get(index);
    int y2 = mYPoints.get(index);
    
    int64_t t1 = mTimestamps.get(index - 1);
    int64_t t2 = mTimestamps.get(index);
    
    float dist = distance(x1, y1, x2, y2);
    float timeDiff = static_cast<float>(t2 - t1);
    
    if (timeDiff <= 0) {
        return 0.0f;
    }
    
    // Return speed in pixels/second
    return (dist / timeDiff) * 1000.0f;
}

float GestureStroke::getAverageSpeed() const {
    if (mXPoints.size() < 2) {
        return 0.0f;
    }
    
    float totalDist = getTotalDistance();
    int64_t duration = getDuration();
    
    if (duration <= 0) {
        return 0.0f;
    }
    
    return (totalDist / static_cast<float>(duration)) * 1000.0f;
}

float GestureStroke::getTotalDistance() const {
    if (mXPoints.size() < 2) {
        return 0.0f;
    }
    
    updateDistances();
    return mCumulativeDistances.empty() ? 0.0f : mCumulativeDistances.back();
}

float GestureStroke::getCumulativeDistance(size_t index) const {
    if (index == 0) {
        return 0.0f;
    }
    
    updateDistances();
    
    if (index >= mCumulativeDistances.size()) {
        return mCumulativeDistances.empty() ? 0.0f : mCumulativeDistances.back();
    }
    
    return mCumulativeDistances[index];
}

int64_t GestureStroke::getDuration() const {
    if (mTimestamps.size() < 2) {
        return 0;
    }
    
    return mTimestamps.get(mTimestamps.size() - 1) - mTimestamps.get(0);
}

void GestureStroke::getBoundingBox(int& outMinX, int& outMinY, int& outMaxX, int& outMaxY) const {
    // Need to update even though it's const - use mutable in real code
    const_cast<GestureStroke*>(this)->updateBoundingBox();
    
    outMinX = mMinX;
    outMinY = mMinY;
    outMaxX = mMaxX;
    outMaxY = mMaxY;
}

void GestureStroke::copyToRecognitionBuffer(int* xBuffer, int* yBuffer, int64_t* timeBuffer,
                                           size_t startIndex, size_t count) const {
    if (!xBuffer || !yBuffer || !timeBuffer) return;
    if (startIndex >= mXPoints.size()) return;
    
    size_t actualCount = std::min(count, mXPoints.size() - startIndex);
    
    mXPoints.copyTo(xBuffer, startIndex, actualCount);
    
    // Manual copy for y and time since IntArrayBuffer::copyTo only works with int*
    for (size_t i = 0; i < actualCount; ++i) {
        yBuffer[i] = mYPoints.get(startIndex + i);
        timeBuffer[i] = mTimestamps.get(startIndex + i);
    }
}

bool GestureStroke::isDuplicatePoint(int x, int y) const {
    if (mXPoints.size() == 0) {
        return false;
    }
    
    int lastX = mXPoints.get(mXPoints.size() - 1);
    int lastY = mYPoints.get(mYPoints.size() - 1);
    
    return distanceSquared(x, y, lastX, lastY) < MIN_POINT_DISTANCE_SQ;
}

bool GestureStroke::getLastPoint(int& outX, int& outY, int64_t& outTime) const {
    if (mXPoints.size() == 0) {
        return false;
    }
    
    size_t lastIdx = mXPoints.size() - 1;
    outX = mXPoints.get(lastIdx);
    outY = mYPoints.get(lastIdx);
    outTime = mTimestamps.get(lastIdx);
    
    return true;
}

float GestureStroke::getAngleAt(size_t index) const {
    if (index == 0 || index >= mXPoints.size()) {
        return 0.0f;
    }
    
    int x1 = mXPoints.get(index - 1);
    int y1 = mYPoints.get(index - 1);
    int x2 = mXPoints.get(index);
    int y2 = mYPoints.get(index);
    
    return std::atan2(static_cast<float>(y2 - y1), static_cast<float>(x2 - x1));
}

float GestureStroke::getCurvatureAt(size_t index) const {
    if (index < 1 || index >= mXPoints.size() - 1) {
        return 0.0f;
    }
    
    // Use three points to estimate curvature
    float angle1 = getAngleAt(index);
    float angle2 = getAngleAt(index + 1);
    
    float angleDiff = angle2 - angle1;
    
    // Normalize to -PI to PI
    while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
    while (angleDiff < -M_PI) angleDiff += 2 * M_PI;
    
    // Curvature = angle change / distance
    float dist = getCumulativeDistance(index + 1) - getCumulativeDistance(index);
    if (dist < 0.001f) {
        return 0.0f;
    }
    
    return angleDiff / dist;
}

void GestureStroke::updateDistances() const {
    if (!mDistancesDirty) {
        return;
    }
    
    auto& self = const_cast<GestureStroke&>(*this);
    
    self.mCumulativeDistances.clear();
    self.mCumulativeDistances.reserve(mXPoints.size());
    
    float cumDist = 0.0f;
    self.mCumulativeDistances.push_back(0.0f);  // First point has 0 distance
    
    for (size_t i = 1; i < mXPoints.size(); ++i) {
        int x1 = mXPoints.get(i - 1);
        int y1 = mYPoints.get(i - 1);
        int x2 = mXPoints.get(i);
        int y2 = mYPoints.get(i);
        
        cumDist += distance(x1, y1, x2, y2);
        self.mCumulativeDistances.push_back(cumDist);
    }
    
    self.mDistancesDirty = false;
}

void GestureStroke::updateBoundingBox() {
    if (!mBoundingBoxDirty) {
        return;
    }
    
    if (mXPoints.size() == 0) {
        mMinX = mMinY = mMaxX = mMaxY = 0;
        mBoundingBoxDirty = false;
        return;
    }
    
    mMinX = mMaxX = mXPoints.get(0);
    mMinY = mMaxY = mYPoints.get(0);
    
    for (size_t i = 1; i < mXPoints.size(); ++i) {
        int x = mXPoints.get(i);
        int y = mYPoints.get(i);
        
        if (x < mMinX) mMinX = x;
        if (x > mMaxX) mMaxX = x;
        if (y < mMinY) mMinY = y;
        if (y > mMaxY) mMaxY = y;
    }
    
    mBoundingBoxDirty = false;
}

float GestureStroke::distance(int x1, int y1, int x2, int y2) {
    float dx = static_cast<float>(x2 - x1);
    float dy = static_cast<float>(y2 - y1);
    return std::sqrt(dx * dx + dy * dy);
}

int GestureStroke::distanceSquared(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return dx * dx + dy * dy;
}

// =============================================================================
// Yandex-style Bezier Smoothing and Speed-based Sampling Implementation
// =============================================================================

SwipePoint GestureStroke::hermiteInterpolate(
    const SwipePoint& p0, const SwipePoint& p1,
    float t0x, float t0y, float t1x, float t1y,
    float t) {
    
    // Hermite basis functions
    float t2 = t * t;
    float t3 = t2 * t;
    
    // h00(t) = 2t³ - 3t² + 1
    // h10(t) = t³ - 2t² + t
    // h01(t) = -2t³ + 3t²
    // h11(t) = t³ - t²
    float h00 = 2*t3 - 3*t2 + 1;
    float h10 = t3 - 2*t2 + t;
    float h01 = -2*t3 + 3*t2;
    float h11 = t3 - t2;
    
    SwipePoint result;
    result.x = h00 * p0.x + h10 * t0x + h01 * p1.x + h11 * t1x;
    result.y = h00 * p0.y + h10 * t0y + h01 * p1.y + h11 * t1y;
    
    // Interpolate timestamp linearly
    result.timestamp = p0.timestamp + static_cast<int64_t>((p1.timestamp - p0.timestamp) * t);
    
    // Distance will be calculated later
    result.cumulativeDistance = 0;
    result.speed = 0;
    
    return result;
}

int GestureStroke::calculateInterpolationSteps(float angleDiff, float segmentLength) const {
    // Yandex algorithm: steps based on angle change AND segment length
    
    // Normalize angle to positive
    float absAngle = std::fabs(angleDiff);
    
    // Steps from angle (more steps for sharper turns)
    int stepsFromAngle = static_cast<int>(std::ceil(absAngle / mParams.maxAngleRadians));
    
    // Steps from length (subdivide long segments)
    int stepsFromLength = static_cast<int>(std::ceil(segmentLength / mParams.maxSegmentLength));
    
    // Take the maximum, but cap it
    int numSteps = std::max(stepsFromAngle, stepsFromLength);
    numSteps = std::max(1, std::min(numSteps, mParams.maxInterpolationSteps));
    
    return numSteps;
}

float GestureStroke::getSegmentSpeed(size_t startIdx, size_t endIdx) const {
    if (startIdx >= mXPoints.size() || endIdx >= mXPoints.size() || startIdx == endIdx) {
        return 0.0f;
    }
    
    int x1 = mXPoints.get(startIdx);
    int y1 = mYPoints.get(startIdx);
    int x2 = mXPoints.get(endIdx);
    int y2 = mYPoints.get(endIdx);
    
    int64_t t1 = mTimestamps.get(startIdx);
    int64_t t2 = mTimestamps.get(endIdx);
    
    float dist = distance(x1, y1, x2, y2);
    float timeDiff = static_cast<float>(t2 - t1);
    
    if (timeDiff <= 0) {
        return 0.0f;
    }
    
    return (dist / timeDiff) * 1000.0f;  // pixels/second
}

std::vector<SwipePoint> GestureStroke::smoothPath() const {
    std::vector<SwipePoint> result;
    
    size_t pointCount = mXPoints.size();
    if (pointCount < 2) {
        // Return raw points if too few
        for (size_t i = 0; i < pointCount; ++i) {
            result.emplace_back(
                static_cast<float>(mXPoints.get(i)),
                static_cast<float>(mYPoints.get(i)),
                mTimestamps.get(i),
                getCumulativeDistance(i),
                i > 0 ? getSpeed(i) : 0.0f
            );
        }
        return result;
    }
    
    // Reserve space (estimate 2-3x due to interpolation)
    result.reserve(pointCount * 3);
    
    // Add first point
    result.emplace_back(
        static_cast<float>(mXPoints.get(0)),
        static_cast<float>(mYPoints.get(0)),
        mTimestamps.get(0),
        0.0f,
        0.0f
    );
    
    // Process each segment with Bezier interpolation
    for (size_t i = 1; i < pointCount; ++i) {
        size_t prevIdx = i - 1;
        size_t prevPrevIdx = (i >= 2) ? i - 2 : 0;
        size_t nextIdx = (i + 1 < pointCount) ? i + 1 : i;
        
        // Current segment endpoints
        float x0 = static_cast<float>(mXPoints.get(prevIdx));
        float y0 = static_cast<float>(mYPoints.get(prevIdx));
        float x1 = static_cast<float>(mXPoints.get(i));
        float y1 = static_cast<float>(mYPoints.get(i));
        
        float dx = x1 - x0;
        float dy = y1 - y0;
        float segmentLength = std::sqrt(dx * dx + dy * dy);
        
        // Skip very short segments
        if (segmentLength < mParams.minSamplingDistance) {
            continue;
        }
        
        // Calculate tangents (Yandex-style: Catmull-Rom)
        float t0x, t0y, t1x, t1y;
        
        // Start tangent
        if (i >= 2) {
            // Use central difference
            float xPrev = static_cast<float>(mXPoints.get(prevPrevIdx));
            float yPrev = static_cast<float>(mYPoints.get(prevPrevIdx));
            t0x = (x1 - xPrev) / 2.0f;
            t0y = (y1 - yPrev) / 2.0f;
        } else {
            // Forward difference
            t0x = dx;
            t0y = dy;
        }
        
        // End tangent
        if (i + 1 < pointCount) {
            float xNext = static_cast<float>(mXPoints.get(nextIdx));
            float yNext = static_cast<float>(mYPoints.get(nextIdx));
            t1x = (xNext - x0) / 2.0f;
            t1y = (yNext - y0) / 2.0f;
        } else {
            // Backward difference
            t1x = dx;
            t1y = dy;
        }
        
        // Calculate angle change for interpolation count
        float angle0 = std::atan2(t0y, t0x);
        float angle1 = std::atan2(t1y, t1x);
        float angleDiff = angle1 - angle0;
        
        // Normalize to [-PI, PI]
        while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
        while (angleDiff < -M_PI) angleDiff += 2 * M_PI;
        
        // Calculate interpolation steps
        int numSteps = calculateInterpolationSteps(angleDiff, segmentLength);
        
        // Create start and end SwipePoints
        SwipePoint p0(x0, y0, mTimestamps.get(prevIdx));
        SwipePoint p1(x1, y1, mTimestamps.get(i));
        
        // Generate interpolated points (skip t=0, we already have that point)
        for (int step = 1; step <= numSteps; ++step) {
            float t = static_cast<float>(step) / static_cast<float>(numSteps);
            SwipePoint interp = hermiteInterpolate(p0, p1, t0x, t0y, t1x, t1y, t);
            result.push_back(interp);
        }
    }
    
    // Recalculate cumulative distances and speeds
    float cumDist = 0.0f;
    for (size_t i = 1; i < result.size(); ++i) {
        float dx = result[i].x - result[i-1].x;
        float dy = result[i].y - result[i-1].y;
        float segDist = std::sqrt(dx * dx + dy * dy);
        cumDist += segDist;
        result[i].cumulativeDistance = cumDist;
        
        // Calculate speed
        int64_t timeDiff = result[i].timestamp - result[i-1].timestamp;
        if (timeDiff > 0) {
            result[i].speed = (segDist / static_cast<float>(timeDiff)) * 1000.0f;
        }
    }
    
    return result;
}

std::vector<SwipePoint> GestureStroke::adaptiveSample(const std::vector<SwipePoint>& smoothedPath) const {
    if (smoothedPath.size() <= 2) {
        return smoothedPath;
    }
    
    std::vector<SwipePoint> result;
    result.reserve(smoothedPath.size());
    
    // Always include first point
    result.push_back(smoothedPath.front());
    
    float speedThreshold = mParams.adaptiveSamplingSpeedThreshold;
    float minDist = mParams.minSamplingDistance;
    
    size_t lastAddedIdx = 0;
    
    for (size_t i = 1; i < smoothedPath.size(); ++i) {
        const SwipePoint& current = smoothedPath[i];
        const SwipePoint& lastAdded = result.back();
        
        // Calculate distance from last added point
        float dx = current.x - lastAdded.x;
        float dy = current.y - lastAdded.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        // Determine sampling density based on speed
        // Low speed (turns) → sample more frequently
        // High speed → sample less frequently
        float samplingDist;
        
        if (current.speed < speedThreshold * 0.5f) {
            // Very slow (sharp turn) - sample at minimum distance
            samplingDist = minDist;
        } else if (current.speed < speedThreshold) {
            // Medium speed - interpolate sampling distance
            float ratio = current.speed / speedThreshold;
            samplingDist = minDist + (minDist * 3.0f - minDist) * ratio;
        } else {
            // High speed - sample less frequently
            float ratio = std::min(2.0f, current.speed / speedThreshold);
            samplingDist = minDist * 3.0f * ratio;
        }
        
        // Also consider angle change (curvature)
        if (i > 1 && i < smoothedPath.size() - 1) {
            const SwipePoint& prev = smoothedPath[i - 1];
            const SwipePoint& next = smoothedPath[i + 1];
            
            float angle1 = std::atan2(current.y - prev.y, current.x - prev.x);
            float angle2 = std::atan2(next.y - current.y, next.x - current.x);
            float angleDiff = std::fabs(angle2 - angle1);
            
            // Normalize
            while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
            angleDiff = std::fabs(angleDiff);
            
            // High curvature → reduce sampling distance
            if (angleDiff > mParams.maxAngleRadians) {
                float curvatureFactor = 1.0f - std::min(1.0f, static_cast<float>(angleDiff / M_PI));
                samplingDist *= curvatureFactor;
                samplingDist = std::max(minDist, samplingDist);
            }
        }
        
        // Add point if distance exceeds threshold
        if (dist >= samplingDist) {
            result.push_back(current);
            lastAddedIdx = i;
        }
    }
    
    // Always include last point
    if (lastAddedIdx != smoothedPath.size() - 1) {
        result.push_back(smoothedPath.back());
    }
    
    return result;
}

std::vector<SwipePoint> GestureStroke::getProcessedPath() const {
    // Step 1: Apply Bezier smoothing
    std::vector<SwipePoint> smoothed = smoothPath();
    
    // Step 2: Apply adaptive sampling based on speed
    std::vector<SwipePoint> sampled = adaptiveSample(smoothed);
    
    return sampled;
}

} // namespace latinime
