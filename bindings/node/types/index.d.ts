// Type definitions for libspotifyctl (Node.js).

/// <reference types="node" />

import { EventEmitter } from 'events';

export const version: string;

export const Status: {
  readonly UNKNOWN:        0;
  readonly STOPPED:        1;
  readonly PAUSED:         2;
  readonly PLAYING:        3;
  readonly CHANGING_TRACK: 4;
};
export type Status = (typeof Status)[keyof typeof Status];

export const AppCommand: {
  readonly STOP:       0;
  readonly PLAY:       1;
  readonly PAUSE:      2;
  readonly PLAY_PAUSE: 3;
  readonly NEXT:       4;
  readonly PREV:       5;
  readonly VOL_UP:     6;
  readonly VOL_DOWN:   7;
  readonly MUTE:       8;
};
export type AppCommand = (typeof AppCommand)[keyof typeof AppCommand];

export interface PlaybackState {
  status:      Status;
  statusName:  'UNKNOWN' | 'STOPPED' | 'PAUSED' | 'PLAYING' | 'CHANGING_TRACK';
  artist:      string;
  title:       string;
  album:       string;
  positionMs:  number;
  durationMs:  number;
  albumArt:    Buffer;
  canSeek:     boolean;
  canSkipNext: boolean;
  canSkipPrev: boolean;
  isAd:        boolean;
  audible:     boolean;
  appMuted:    boolean;
  appVolume:   number;
}

export interface SpotifyClientEvents {
  opened: () => void;
  closed: () => void;
  /**
   * Emitted on every unified-state change. Also emitted synchronously once
   * on the first attach of any listener after `start()`, carrying the client's
   * current snapshot — so late subscribers don't need to race against
   * `latestState()`.
   */
  stateChanged: (state: PlaybackState) => void;
  audibleChanged: (audible: boolean) => void;
  rawTitle: (title: string) => void;
  /** Emitted when the (artist, title, album) tuple changes. */
  trackChanged: (previous: PlaybackState, current: PlaybackState) => void;
  /** Emitted on `isAd` false→true. */
  adStarted: () => void;
  /** Emitted on `isAd` true→false. */
  adEnded: () => void;
  /** ~1 Hz while Playing AND at least one listener is attached. */
  positionChanged: (positionMs: number) => void;
}

export declare class SpotifyClient extends EventEmitter {
  constructor();

  start(): void;
  stop(): void;
  close(): void;

  readonly isRunning: boolean;

  play(): boolean;
  pause(): boolean;
  next(): boolean;
  previous(): boolean;
  seekMs(positionMs: number): boolean;
  sendCommand(cmd: AppCommand | number): boolean;
  openUri(uri: string): boolean;
  sendKey(virtualKey: number): boolean;

  appVolume: number;
  appMuted: boolean;
  readonly peakAmplitude: number;

  latestState(): PlaybackState;
  latestStateJson(): string;

  /** Current position with monotonic-clock extrapolation while Playing. */
  readonly positionSmoothMs: number;

  disconnect(token: bigint | number): void;

  on<K extends keyof SpotifyClientEvents>(event: K, listener: SpotifyClientEvents[K]): this;
  off<K extends keyof SpotifyClientEvents>(event: K, listener: SpotifyClientEvents[K]): this;
  once<K extends keyof SpotifyClientEvents>(event: K, listener: SpotifyClientEvents[K]): this;
  emit<K extends keyof SpotifyClientEvents>(event: K, ...args: Parameters<SpotifyClientEvents[K]>): boolean;
}

export function uriTrack(id: string): string;
export function uriAlbum(id: string): string;
export function uriPlaylist(id: string): string;
export function uriArtist(id: string): string;
export function uriUser(username: string): string;
export function uriSearch(query: string): string;
