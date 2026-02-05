/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * GestureStroke - Storage and processing of gesture/swipe input
 * Enhanced with Yandex-style Bezier smoothing and speed-based sampling
 */

#ifndef HOSKEY_GESTURE_STROKE_H
#define HOSKEY_GESTURE_STROKE_H

#include <vector>
#include <cstdint>
#include <cmath>

namespace latinime {

/**
 * Parameters for gesture detection and processing
 * (Enhanced with Yandex-style parameters)
 */
struct GestureParams {
    // Minimum distance to move before gesture mode activates (pixels)
    int minGestureLength = 60;
    
    // Minimum sampling length (number of points to collect)
    int minSamplingLength = 128;
    
    // Speed threshold to detect gesture start (pixels/second)
    int speedThreshold = 200;
    
    // Time threshold for fast move detection (ms)
    int64_t fastMoveTime = 100;
    
    // Distance threshold for fast move (pixels)
    int fastMoveDistance = 20;
    
    // Recognition update interval (ms) - for rate limiting
    int64_t updateIntervalMs = 50;
    
    // Detection threshold (confidence 0.0-1.0)
    float detectionThreshold = 0.5f;
    
    // Maximum time between points before resetting (ms)
    int64_t maxPointInterval = 500;
    
    // Bezier smoothing factor (0.0 = no smoothing, 1.0 = maximum)
    float smoothingFactor = 0.3f;
    
    // === Yandex-style parameters (from json_config.json) ===
    
    // Minimum distance between sampled points (pixels)
    // Yandex: SamplingDistance = 0.1 (very dense)
    // We use slightly larger for performance
    float minSamplingDistance = 0.5f;  // Changed from 3.0f to match Yandex
    
    // Maximum angle change between segments (radians)
    // Controls smoothness of trail curves
    float maxAngleRadians = 0.2618f;  // ~15 degrees
    
    // Maximum segment length for trail drawing
    float maxSegmentLength = 20.0f;
    
    // Maximum interpolation steps between points
    int maxInterpolationSteps = 10;
    
    // Speed-based sampling: min points at high speed
    int minPointsPerSegment = 2;
    
    // Speed-based sampling: max points at low speed (turns)
    int maxPointsPerSegment = 8;
    
    // Speed threshold for adaptive sampling (pixels/second)
    // Below this speed, more points are sampled
    float adaptiveSamplingSpeedThreshold = 300.0f;
    
    // === Yandex Swipe/Rule parameters ===
    
    // Key detection X weight (Yandex: StartKeyDistanceWeightX = 0.8)
    float keyDistanceWeightX = 0.8f;
    
    // Key detection Y weight (Yandex: StartKeyDistanceWeightY = 1.4)
    float keyDistanceWeightY = 1.4f;
    
    // Maximum squared distance to consider key hit (Yandex: KeySquaredDistanceLimit = 3000)
    float keySquaredDistanceLimit = 3000.0f;
    
    // Maximum transition squared distance (Yandex: MaxTransitionSquaredDistance = 10000)
    float maxTransitionSquaredDistance = 10000.0f;
    
    // Beam width for candidate search (Yandex: BeamWidth = 300)
    int beamWidth = 300;
    
    // TopK candidates (Yandex: Swipe/Linear TopK = 24)
    int topK = 24;
};

/**
 * A single point in a gesture path (for smoothing output)
 */
struct SwipePoint {
    float x;
    float y;
    int64_t timestamp;
    float cumulativeDistance;
    float speed;  // pixels/second at this point
    
    SwipePoint() : x(0), y(0), timestamp(0), cumulativeDistance(0), speed(0) {}
    SwipePoint(float px, float py, int64_t ts = 0, float dist = 0, float spd = 0)
        : x(px), y(py), timestamp(ts), cumulativeDistance(dist), speed(spd) {}
};

/**
 * Buffer for efficient integer array operations
 */
class IntArrayBuffer {
public:
    explicit IntArrayBuffer(size_t initialCapacity = 256);
    ~IntArrayBuffer();
    
    void add(int value);
    int get(size_t index) const;
    void set(size_t index, int value);
    size_t size() const { return mSize; }
    void clear();
    void reserve(size_t capacity);
    
    int* data() { return mData.data(); }
    const int* data() const { return mData.data(); }
    
    // Copy a range to another buffer
    void copyTo(int* dest, size_t start, size_t count) const;
    
private:
    std::vector<int> mData;
    size_t mSize;
};

/**
 * Buffer for timestamps (64-bit)
 */
class LongArrayBuffer {
public:
    explicit LongArrayBuffer(size_t initialCapacity = 256);
    ~LongArrayBuffer();
    
    void add(int64_t value);
    int64_t get(size_t index) const;
    void set(size_t index, int64_t value);
    size_t size() const { return mSize; }
    void clear();
    void reserve(size_t capacity);
    
    int64_t* data() { return mData.data(); }
    const int64_t* data() const { return mData.data(); }
    
private:
    std::vector<int64_t> mData;
    size_t mSize;
};

/**
 * GestureStroke - Stores and processes gesture input points
 * 
 * This class collects touch points during a swipe gesture and provides
 * methods for:
 * - Speed detection (to determine when gesture mode should start)
 * - Distance calculation
 * - Buffering for recognition engine
 * - Trail rendering support
 */
class GestureStroke {
public:
    GestureStroke();
    explicit GestureStroke(const GestureParams& params);
    ~GestureStroke();
    
    /**
     * Add a new point to the gesture
     * @param x X coordinate (pixels)
     * @param y Y coordinate (pixels)
     * @param time Timestamp (milliseconds)
     * @return true if the point was added (not a duplicate)
     */
    bool addPoint(int x, int y, int64_t time);
    
    /**
     * Reset the stroke for a new gesture
     */
    void reset();
    
    /**
     * Get the number of points in the stroke
     */
    size_t getPointCount() const { return mXPoints.size(); }
    
    /**
     * Check if the stroke has enough points for recognition
     */
    bool hasEnoughPoints() const;
    
    /**
     * Check if gesture mode should be activated based on speed
     * Call this after adding first few points
     */
    bool shouldActivateGestureMode() const;
    
    /**
     * Get instantaneous speed at a point (pixels/second)
     * @param index Point index (0 = first point)
     * @return Speed in pixels/second, or 0 if can't calculate
     */
    float getSpeed(size_t index) const;
    
    /**
     * Get average speed of the gesture
     */
    float getAverageSpeed() const;
    
    /**
     * Get total distance traveled
     */
    float getTotalDistance() const;
    
    /**
     * Get cumulative distance up to a point
     */
    float getCumulativeDistance(size_t index) const;
    
    /**
     * Get gesture duration (ms)
     */
    int64_t getDuration() const;
    
    /**
     * Get bounding box
     */
    void getBoundingBox(int& outMinX, int& outMinY, int& outMaxX, int& outMaxY) const;
    
    /**
     * Copy points to recognition buffer
     * @param sharedBuffer Buffer to copy to (should be pre-allocated)
     * @param startIndex Start index in stroke
     * @param count Number of points to copy
     */
    void copyToRecognitionBuffer(int* xBuffer, int* yBuffer, int64_t* timeBuffer,
                                size_t startIndex, size_t count) const;
    
    /**
     * Get raw point data for rendering
     */
    const int* getXPoints() const { return mXPoints.data(); }
    const int* getYPoints() const { return mYPoints.data(); }
    const int64_t* getTimestamps() const { return mTimestamps.data(); }
    
    /**
     * Get parameters
     */
    const GestureParams& getParams() const { return mParams; }
    void setParams(const GestureParams& params) { mParams = params; }
    
    /**
     * Check if a point at given coordinates would be a duplicate
     * (within threshold distance of last point)
     */
    bool isDuplicatePoint(int x, int y) const;
    
    /**
     * Get the last point added
     */
    bool getLastPoint(int& outX, int& outY, int64_t& outTime) const;
    
    /**
     * Calculate angle at a specific point (radians)
     */
    float getAngleAt(size_t index) const;
    
    /**
     * Calculate curvature at a specific point
     */
    float getCurvatureAt(size_t index) const;
    
    // =========================================================================
    // Yandex-style Bezier Smoothing and Speed-based Sampling
    // =========================================================================
    
    /**
     * Smooth the gesture path using cubic Bezier interpolation
     * (Yandex-style: GestureTrailData.copyFromDrawer algorithm)
     * 
     * @return Vector of smoothed points with interpolated positions
     */
    std::vector<SwipePoint> smoothPath() const;
    
    /**
     * Apply speed-based adaptive sampling
     * - Fast segments → fewer points (user moving quickly)
     * - Slow segments → more points (turns, precise movements)
     * 
     * @param smoothedPath Input path (can be raw or pre-smoothed)
     * @return Adaptively sampled path
     */
    std::vector<SwipePoint> adaptiveSample(const std::vector<SwipePoint>& smoothedPath) const;
    
    /**
     * Get processed path ready for recognition
     * Combines smoothing + adaptive sampling
     * 
     * @return Optimized path for swipe recognition
     */
    std::vector<SwipePoint> getProcessedPath() const;
    
    /**
     * Cubic Hermite interpolation between two points
     * 
     * @param p0 Start point
     * @param p1 End point
     * @param t0 Tangent at start
     * @param t1 Tangent at end
     * @param t Parameter [0.0, 1.0]
     * @return Interpolated point
     */
    static SwipePoint hermiteInterpolate(
        const SwipePoint& p0, const SwipePoint& p1,
        float t0x, float t0y, float t1x, float t1y,
        float t);
    
    /**
     * Calculate number of interpolation steps for a segment
     * Based on angle change and segment length (Yandex algorithm)
     * 
     * @param angleDiff Angle difference in radians
     * @param segmentLength Length in pixels
     * @return Number of interpolation steps
     */
    int calculateInterpolationSteps(float angleDiff, float segmentLength) const;
    
    /**
     * Get speed at a segment (between two points)
     * 
     * @param startIdx Start point index
     * @param endIdx End point index  
     * @return Speed in pixels/second
     */
    float getSegmentSpeed(size_t startIdx, size_t endIdx) const;
    
private:
    IntArrayBuffer mXPoints;
    IntArrayBuffer mYPoints;
    LongArrayBuffer mTimestamps;
    
    // Cached cumulative distances
    std::vector<float> mCumulativeDistances;
    bool mDistancesDirty;
    
    // Bounding box (cached)
    int mMinX, mMinY, mMaxX, mMaxY;
    bool mBoundingBoxDirty;
    
    GestureParams mParams;
    
    // Minimum distance to consider a new point (avoid duplicates)
    static constexpr int MIN_POINT_DISTANCE_SQ = 4;  // 2 pixels squared
    
    /**
     * Update cached distances
     */
    void updateDistances() const;
    
    /**
     * Update bounding box
     */
    void updateBoundingBox();
    
    /**
     * Calculate distance between two points
     */
    static float distance(int x1, int y1, int x2, int y2);
    
    /**
     * Calculate squared distance (faster, no sqrt)
     */
    static int distanceSquared(int x1, int y1, int x2, int y2);
};

/**
 * Recognition buffer - shared buffer for sending to recognition engine
 */
struct RecognitionBuffer {
    static constexpr size_t MAX_POINTS = 512;
    
    int xCoords[MAX_POINTS];
    int yCoords[MAX_POINTS];
    int64_t timestamps[MAX_POINTS];
    size_t pointCount;
    
    RecognitionBuffer() : pointCount(0) {
        std::fill(xCoords, xCoords + MAX_POINTS, 0);
        std::fill(yCoords, yCoords + MAX_POINTS, 0);
        std::fill(timestamps, timestamps + MAX_POINTS, 0);
    }
    
    void clear() { pointCount = 0; }
};

} // namespace latinime

#endif // HOSKEY_GESTURE_STROKE_H
