/**
 * Input Pipeline Module - Barrel Export
 *
 * Central export point for all input pipeline components.
 * Use: import { InputPipeline, InputEvent, ... } from './model/input'
 */

// Core pipeline
export { InputPipeline } from './InputPipeline';

// Types and interfaces
export {
  InputMode,
  ITextClientAdapter,
  ISuggestionPresenter,
  IPredictor
} from './InputTypes';

export type {
  InputEvent,
  KeyTapEvent,
  BackspaceEvent,
  SpaceEvent,
  EnterEvent,
  SwipeStartEvent,
  SwipeMoveEvent,
  SwipeEndEvent
} from './InputTypes';

// Utilities
export { TextBuffer } from './TextBuffer';
export { Debouncer } from './Debouncer';
export { HarmonyTextClientAdapter } from './HarmonyTextClientAdapter';
export { PredictorFacade } from './PredictorFacade';
