import { BaseLexicon } from './BaseLexicon';

// === INTERFACES ===

export interface SwipePoint {
  x: number;
  y: number;
  timestamp: number;
}

export interface KeyBounds {
  key: string;
  centerX: number;
  centerY: number;
  width: number;
  height: number;
}

interface SwipeCandidate {
  word: string;
  distance: number;
  frequency: number;
  score: number;
}

export interface SwipeResult {
  bestWord: string;
  alternatives: string[];
  confidence: number;
  rawSequence: string;
}

// === CLASS ===

export class SwipeEngine {
  private keyboardLayout: KeyBounds[] = [];
  private baseLexicon: BaseLexicon | null = null;

  // Algorithm parameters
  private readonly MIN_SWIPE_POINTS: number = 5;
  private readonly MIN_SWIPE_DISTANCE: number = 50;
  private readonly MAX_CANDIDATES: number = 50;
  private readonly PATH_SAMPLE_INTERVAL: number = 10;
  private readonly KEY_HIT_RADIUS: number = 1.5;

  constructor() {}

  setKeyboardLayout(keys: KeyBounds[]): void {
    this.keyboardLayout = keys;
  }

  setBaseLexicon(lexicon: BaseLexicon | null): void {
    this.baseLexicon = lexicon;
  }

  processSwipe(rawPath: SwipePoint[]): SwipeResult | null {
    if (!this.isValidSwipe(rawPath)) {
      return null;
    }

    const smoothedPath = this.smoothPath(rawPath);
    const sampledPath = this.samplePath(smoothedPath);
    const keySequence = this.pathToKeySequence(sampledPath);

    if (keySequence.length < 2) {
      return null;
    }

    const candidates = this.getCandidates(keySequence);

    if (candidates.length === 0) {
      return {
        bestWord: this.collapseSequence(keySequence),
        alternatives: [],
        confidence: 0.3,
        rawSequence: keySequence.join('')
      };
    }

    const rankedCandidates = this.rankCandidatesByPath(candidates, sampledPath);
    const best = rankedCandidates[0];

    return {
      bestWord: best.word,
      alternatives: rankedCandidates.slice(1, 4).map(c => c.word),
      confidence: this.calculateConfidence(best, rankedCandidates),
      rawSequence: keySequence.join('')
    };
  }

  private isValidSwipe(path: SwipePoint[]): boolean {
    if (path.length < this.MIN_SWIPE_POINTS) return false;

    let totalDistance = 0;
    for (let i = 1; i < path.length; i++) {
      totalDistance += this.distance(path[i-1], path[i]);
    }

    return totalDistance >= this.MIN_SWIPE_DISTANCE;
  }

  private smoothPath(path: SwipePoint[]): SwipePoint[] {
    if (path.length < 3) return path;

    const smoothed: SwipePoint[] = [path[0]];
    const windowSize = 3;

    for (let i = 1; i < path.length - 1; i++) {
      let sumX = 0;
      let sumY = 0;
      let count = 0;

      const start = Math.max(0, i - windowSize);
      const end = Math.min(path.length - 1, i + windowSize);

      for (let j = start; j <= end; j++) {
        sumX += path[j].x;
        sumY += path[j].y;
        count++;
      }

      smoothed.push({
        x: sumX / count,
        y: sumY / count,
        timestamp: path[i].timestamp
      });
    }

    smoothed.push(path[path.length - 1]);
    return smoothed;
  }

  private samplePath(path: SwipePoint[]): SwipePoint[] {
    if (path.length < 2) return path;

    const sampled: SwipePoint[] = [path[0]];
    let accumulatedDist = 0;

    for (let i = 1; i < path.length; i++) {
      const dist = this.distance(path[i-1], path[i]);
      accumulatedDist += dist;

      if (accumulatedDist >= this.PATH_SAMPLE_INTERVAL) {
        sampled.push(path[i]);
        accumulatedDist = 0;
      }
    }

    const lastPoint = path[path.length - 1];
    const lastSampled = sampled[sampled.length - 1];
    if (lastPoint.x !== lastSampled.x || lastPoint.y !== lastSampled.y) {
      sampled.push(lastPoint);
    }

    return sampled;
  }

  private pathToKeySequence(path: SwipePoint[]): string[] {
    const sequence: string[] = [];

    for (const point of path) {
      const key = this.findNearestKey(point);
      if (key && (sequence.length === 0 || key !== sequence[sequence.length - 1])) {
        sequence.push(key);
      }
    }

    return sequence;
  }

  private findNearestKey(point: SwipePoint): string | null {
    let nearest: string | null = null;
    let minDist = Infinity;

    for (const keyBounds of this.keyboardLayout) {
      const dist = this.distance(
        point,
        { x: keyBounds.centerX, y: keyBounds.centerY, timestamp: 0 }
      );

      const maxDist = Math.max(keyBounds.width, keyBounds.height) * this.KEY_HIT_RADIUS;

      if (dist < minDist && dist < maxDist) {
        minDist = dist;
        nearest = keyBounds.key;
      }
    }

    return nearest;
  }

  private getCandidates(keySequence: string[]): SwipeCandidate[] {
    if (!this.baseLexicon || !this.baseLexicon.isLoaded()) {
      return [];
    }

    const candidates: SwipeCandidate[] = [];
    const firstKey = keySequence[0];
    const lastKey = keySequence[keySequence.length - 1];
    const seqLength = keySequence.length;

    const words = this.baseLexicon.findByPrefix(firstKey, this.MAX_CANDIDATES * 3);

    for (const entry of words) {
      const word = entry.word;

      if (word.length < seqLength * 0.5 || word.length > seqLength * 1.5) continue;
      if (word[word.length - 1] !== lastKey) continue;
      if (!this.containsMostKeys(word, keySequence)) continue;

      candidates.push({
        word: word,
        distance: 0,
        frequency: entry.freq,
        score: 0
      });

      if (candidates.length >= this.MAX_CANDIDATES) break;
    }

    return candidates;
  }

  private containsMostKeys(word: string, keySequence: string[]): boolean {
    const uniqueKeys = new Set(keySequence);
    let matched = 0;

    for (const key of uniqueKeys) {
      if (word.includes(key)) matched++;
    }

    return matched >= uniqueKeys.size * 0.6;
  }

  private rankCandidatesByPath(candidates: SwipeCandidate[], actualPath: SwipePoint[]): SwipeCandidate[] {
    for (const candidate of candidates) {
      const idealPath = this.buildIdealPath(candidate.word);
      candidate.distance = this.dtwDistance(actualPath, idealPath);
      candidate.score = (1 / (candidate.distance + 1)) * 1000 + candidate.frequency;
    }

    return candidates.sort((a, b) => b.score - a.score);
  }

  private buildIdealPath(word: string): SwipePoint[] {
    const path: SwipePoint[] = [];

    for (const char of word) {
      const keyBounds = this.keyboardLayout.find(k => k.key === char);
      if (keyBounds) {
        path.push({
          x: keyBounds.centerX,
          y: keyBounds.centerY,
          timestamp: 0
        });
      }
    }

    return path;
  }

  private dtwDistance(path1: SwipePoint[], path2: SwipePoint[]): number {
    const n = path1.length;
    const m = path2.length;

    if (n === 0 || m === 0) return Infinity;

    const dtw: number[][] = [];
    for (let i = 0; i <= n; i++) {
      dtw[i] = [];
      for (let j = 0; j <= m; j++) {
        dtw[i][j] = Infinity;
      }
    }
    dtw[0][0] = 0;

    for (let i = 1; i <= n; i++) {
      for (let j = 1; j <= m; j++) {
        const cost = this.distance(path1[i-1], path2[j-1]);
        dtw[i][j] = cost + Math.min(
          dtw[i-1][j],
          dtw[i][j-1],
          dtw[i-1][j-1]
        );
      }
    }

    return dtw[n][m] / Math.max(n, m);
  }

  private distance(p1: SwipePoint, p2: SwipePoint): number {
    const dx = p1.x - p2.x;
    const dy = p1.y - p2.y;
    return Math.sqrt(dx * dx + dy * dy);
  }

  private collapseSequence(sequence: string[]): string {
    let result = '';
    for (const char of sequence) {
      if (result.length === 0 || result[result.length - 1] !== char) {
        result += char;
      }
    }
    return result;
  }

  private calculateConfidence(best: SwipeCandidate, all: SwipeCandidate[]): number {
    if (all.length < 2) return 0.9;

    const scoreDiff = best.score - all[1].score;
    const relDiff = scoreDiff / best.score;

    return Math.min(0.95, 0.5 + relDiff);
  }
}

export const swipeEngine = new SwipeEngine();
