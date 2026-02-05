/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 *
 * GestureTrailData - Yandex-style gesture trail storage and rendering
 *
 * Features:
 * - Bezier interpolation for smooth curves
 * - Time-based fade effect
 * - Width tapering (thick → thin)
 * - Segment break markers for multi-gesture
 * - Efficient buffer compaction
 */

#ifndef HOSKEY_GESTURE_TRAIL_DATA_H
#define HOSKEY_GESTURE_TRAIL_DATA_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace latinime {

/**
 * Trail rendering parameters (like Yandex TrailRenderParams)
 */
struct TrailRenderParams {
    // Trail width
    float maxWidth = 12.0f;         // Width at start of fade
    float minWidth = 2.0f;          // Width at end of fade
    float widthRatio = 1.0f;        // Overall width multiplier

    // Fade timing
    int fadeStartTimeMs = 100;      // When fade begins (ms after point creation)
    int fadeDurationMs = 400;       // How long fade takes
    int totalLifetimeMs = 500;      // fadeStartTimeMs + fadeDurationMs

    // Colors (ARGB format)
    uint32_t trailColor = 0xFF4285F4;  // Google blue default

    // Shadow/glow effect
    bool shadowEnabled = true;
    float shadowRadiusRatio = 0.3f;  // Shadow radius as ratio of trail width

    TrailRenderParams() {
        totalLifetimeMs = fadeStartTimeMs + fadeDurationMs;
    }
};

/**
 * Trail interpolation parameters (like Yandex TrailParams)
 * Aligned with Yandex json_config.json Swipe parameters
 */
struct TrailParams {
    // Minimum distance between sampled points
    // Yandex SamplingDistance = 0.1, we use 0.5 for balance
    float minSamplingDistance = 0.5f;

    // Maximum angle change before adding interpolation points
    float maxAngleRadians = 0.2618f;  // ~15 degrees

    // Maximum segment length before subdivision
    float maxSegmentLength = 20.0f;

    // Maximum interpolation steps per segment
    int maxInterpolationSteps = 10;
    
    // Yandex Swipe/Rule parameters
    float keyDistanceWeightX = 0.8f;   // StartKeyDistanceWeightX
    float keyDistanceWeightY = 1.4f;   // StartKeyDistanceWeightY
};

/**
 * A single trail point with all necessary data
 */
struct TrailPoint {
    float x;
    float y;
    int32_t relativeTime;  // Time relative to gesture start (ms)

    TrailPoint() : x(0), y(0), relativeTime(0) {}
    TrailPoint(float px, float py, int32_t time) : x(px), y(py), relativeTime(time) {}
};

/**
 * Bezier interpolation helper (like Yandex BezierInterpolator)
 */
class BezierInterpolator {
public:
    // Current segment endpoints
    float x0, y0;
    float x1, y1;

    // Tangent vectors at endpoints
    float tangentX0, tangentY0;
    float tangentX1, tangentY1;

    // Source point arrays for tangent calculation
    const float* xPoints = nullptr;
    const float* yPoints = nullptr;
    size_t pointCount = 0;

    BezierInterpolator() : x0(0), y0(0), x1(0), y1(0),
        tangentX0(0), tangentY0(0), tangentX1(0), tangentY1(0) {}

    /**
     * Hermite interpolation at parameter t [0, 1]
     */
    void interpolate(float t, float& outX, float& outY) const {
        float oneMinusT = 1.0f - t;
        float tDouble = t * 2.0f;

        // Hermite basis functions (Yandex style)
        float h1 = tDouble + 1.0f;
        float h2 = 3.0f - tDouble;
        float oneMinusT2 = oneMinusT * oneMinusT;
        float t2 = t * t;

        outX = ((x1 * h2 - tangentX1 * oneMinusT) * t2) +
               ((tangentX0 * t + x0 * h1) * oneMinusT2);
        outY = ((y1 * h2 - tangentY1 * oneMinusT) * t2) +
               ((tangentY0 * t + y0 * h1) * oneMinusT2);
    }
};

/**
 * GestureTrailData - Main trail storage and processing class
 *
 * Equivalent to Yandex's ff.k (C3523k)
 */
class GestureTrailData {
public:
    GestureTrailData();
    ~GestureTrailData();

    /**
     * Copy and interpolate points from raw input
     * Applies Bezier smoothing based on angle and length
     *
     * @param xPoints Raw X coordinates
     * @param yPoints Raw Y coordinates
     * @param timestamps Raw timestamps (ms)
     * @param pointCount Number of points
     * @param currentTime Current system time (ms)
     * @param params Trail interpolation parameters
     */
    void copyFromRawPoints(
        const int* xPoints,
        const int* yPoints,
        const int64_t* timestamps,
        size_t pointCount,
        int64_t currentTime,
        const TrailParams& params);

    /**
     * Calculate visible points and fade levels
     * Call before rendering to update visibility
     *
     * @param currentTime Current system time (ms)
     * @param params Render parameters
     * @return Number of visible points
     */
    int updateVisibility(int64_t currentTime, const TrailRenderParams& params);

    /**
     * Get trail segments for rendering
     * Each segment has: x0, y0, width0, x1, y1, width1, alpha
     *
     * @param currentTime Current system time (ms)
     * @param params Render parameters
     * @param outSegments Output vector of segments (7 floats each)
     * @return Number of segments
     */
    int getSegmentsForRendering(
        int64_t currentTime,
        const TrailRenderParams& params,
        std::vector<float>& outSegments);

    /**
     * Compact buffers by removing expired points
     */
    void compactBuffers();

    /**
     * Reset all data for new gesture
     */
    void reset();

    /**
     * Check if trail has any visible points
     */
    bool hasVisiblePoints() const { return mVisibleCount > 0; }

    /**
     * Get total point count (including expired)
     */
    size_t getPointCount() const { return mPoints.size(); }

    /**
     * Get visible point count
     */
    size_t getVisibleCount() const { return mVisibleCount; }

    /**
     * Set pointer ID for this trail
     */
    void setPointerId(int id) { mPointerId = id; }
    int getPointerId() const { return mPointerId; }

    /**
     * Get gesture start time
     */
    int64_t getGestureStartTime() const { return mGestureStartTime; }

    // === Static Constants ===

    // Marker for segment break (when pointer changes)
    static constexpr int32_t SEGMENT_BREAK_MARKER = -128;

private:
    // Point storage
    std::vector<TrailPoint> mPoints;

    // Interpolation helper
    BezierInterpolator mBezier;

    // Trail state
    int mPointerId = -1;
    int64_t mGestureStartTime = 0;
    size_t mFirstVisibleIndex = 0;
    size_t mLastInterpolatedIndex = 0;
    size_t mVisibleCount = 0;

    // Last processed raw point index (for incremental updates)
    size_t mLastRawIndex = 0;

    /**
     * Calculate tangent at a point using central/forward/backward difference
     */
    void calculateTangent(
        const float* xData, const float* yData, size_t count,
        size_t prevIdx, size_t currIdx, size_t nextIdx,
        float dx, float dy,
        float& outTx, float& outTy);

    /**
     * Calculate number of interpolation steps for a segment
     */
    int calculateSteps(float angleDiff, float segmentLength, const TrailParams& params);

    /**
     * Insert interpolated point at index
     */
    void insertPoint(size_t index, float x, float y, int32_t time);

    /**
     * Check if point is a segment break
     */
    bool isSegmentBreak(size_t index) const;

    /**
     * Mark point as segment break
     */
    void markSegmentBreak(size_t index);
};

/**
 * GestureTrailDrawer - Collects raw points during gesture
 * Equivalent to Yandex's ff.g (C3519g)
 */
class GestureTrailDrawer {
public:
    GestureTrailDrawer();
    ~GestureTrailDrawer();

    /**
     * Add a new point to the trail
     */
    void addPoint(int x, int y, int64_t timestamp);

    /**
     * Reset for new gesture
     */
    void reset();

    /**
     * Get raw point data
     */
    const std::vector<int>& getXBuffer() const { return mXBuffer; }
    const std::vector<int>& getYBuffer() const { return mYBuffer; }
    const std::vector<int64_t>& getTimeBuffer() const { return mTimeBuffer; }
    size_t getPointCount() const { return mPointCount; }

    /**
     * Get/set last render index (for incremental updates)
     */
    size_t getLastRenderIndex() const { return mLastRenderIndex; }
    void setLastRenderIndex(size_t idx) { mLastRenderIndex = idx; }

    /**
     * Set pointer ID
     */
    void setPointerId(int id) { mPointerId = id; }
    int getPointerId() const { return mPointerId; }

    /**
     * Get trail params
     */
    const TrailParams& getParams() const { return mParams; }
    void setParams(const TrailParams& params) { mParams = params; }

private:
    std::vector<int> mXBuffer;
    std::vector<int> mYBuffer;
    std::vector<int64_t> mTimeBuffer;
    size_t mPointCount = 0;
    size_t mLastRenderIndex = 0;
    int mPointerId = -1;
    TrailParams mParams;

    // Minimum distance to add new point
    static constexpr float MIN_POINT_DISTANCE = 2.0f;
};

} // namespace latinime

#endif // HOSKEY_GESTURE_TRAIL_DATA_H
