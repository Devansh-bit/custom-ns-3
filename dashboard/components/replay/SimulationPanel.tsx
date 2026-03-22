"use client";

import React, { useState, useEffect, useCallback } from 'react';
import { SkipBack, Pause, Play, SkipForward, RotateCcw } from 'lucide-react';
import { Slider } from '@/components/ui/slider';
import {
  fetchReplayState,
  replayPlay,
  replayPause,
  replaySeek,
  replaySetSpeed,
  replayReset,
  type ReplayState
} from '@/lib/api-dashboard';

interface SimulationPanelProps {
  onStateChange?: (state: ReplayState) => void;
}

function formatTime(seconds: number): string {
  const mins = Math.floor(seconds / 60);
  const secs = Math.floor(seconds % 60);
  return `${mins}:${secs.toString().padStart(2, '0')}`;
}

export const SimulationPanel: React.FC<SimulationPanelProps> = ({ onStateChange }) => {
  const [replayState, setReplayState] = useState<ReplayState>({
    current_time: 10,
    is_playing: false,
    speed: 1,
    min_time: 10,
    max_time: 1800,
  });
  const [sliderValue, setSliderValue] = useState([0]);
  const [isDragging, setIsDragging] = useState(false);

  const speeds = [1, 2, 5, 10, 20];
  const speedIndex = speeds.indexOf(replayState.speed);

  // Fetch initial replay state and poll for updates
  useEffect(() => {
    let mounted = true;
    let pollInterval: NodeJS.Timeout;

    const fetchState = async () => {
      try {
        const state = await fetchReplayState();
        if (mounted && !isDragging) {
          setReplayState(state);
          // Convert time to slider percentage
          const percentage = ((state.current_time - state.min_time) / (state.max_time - state.min_time)) * 100;
          setSliderValue([percentage]);
          onStateChange?.(state);
        }
      } catch (error) {
        console.error('Failed to fetch replay state:', error);
      }
    };

    fetchState();
    // Poll every 500ms when playing, 2s when paused
    pollInterval = setInterval(fetchState, replayState.is_playing ? 500 : 2000);

    return () => {
      mounted = false;
      clearInterval(pollInterval);
    };
  }, [replayState.is_playing, isDragging, onStateChange]);

  const handlePlayPause = useCallback(async () => {
    try {
      if (replayState.is_playing) {
        await replayPause();
      } else {
        await replayPlay();
      }
      const newState = await fetchReplayState();
      setReplayState(newState);
    } catch (error) {
      console.error('Failed to toggle play/pause:', error);
    }
  }, [replayState.is_playing]);

  const handleSpeedCycle = useCallback(async () => {
    const nextIndex = (speedIndex + 1) % speeds.length;
    const newSpeed = speeds[nextIndex];
    try {
      await replaySetSpeed(newSpeed);
      setReplayState(prev => ({ ...prev, speed: newSpeed }));
    } catch (error) {
      console.error('Failed to set speed:', error);
    }
  }, [speedIndex, speeds]);

  const handleSeek = useCallback(async (percentage: number) => {
    const newTime = replayState.min_time + (percentage / 100) * (replayState.max_time - replayState.min_time);
    try {
      await replaySeek(newTime);
      const newState = await fetchReplayState();
      setReplayState(newState);
    } catch (error) {
      console.error('Failed to seek:', error);
    }
  }, [replayState.min_time, replayState.max_time]);

  const handleSliderChange = (value: number[]) => {
    setSliderValue(value);
  };

  const handleSliderCommit = (value: number[]) => {
    setIsDragging(false);
    handleSeek(value[0]);
  };

  const handlePrevious = useCallback(async () => {
    // Jump back 30 seconds
    const newTime = Math.max(replayState.min_time, replayState.current_time - 30);
    await replaySeek(newTime);
    const newState = await fetchReplayState();
    setReplayState(newState);
  }, [replayState.current_time, replayState.min_time]);

  const handleNext = useCallback(async () => {
    // Jump forward 30 seconds
    const newTime = Math.min(replayState.max_time, replayState.current_time + 30);
    await replaySeek(newTime);
    const newState = await fetchReplayState();
    setReplayState(newState);
  }, [replayState.current_time, replayState.max_time]);

  const handleReset = useCallback(async () => {
    try {
      await replayReset();
      const newState = await fetchReplayState();
      setReplayState(newState);
      setSliderValue([0]);
    } catch (error) {
      console.error('Failed to reset:', error);
    }
  }, []);

  return (
    <div className="fixed bottom-4 left-1/2 -translate-x-1/2 z-50">
      <div className="bg-background/95 backdrop-blur-sm border-2 border-primary/20 rounded-lg shadow-2xl px-4 py-3 space-y-3">
        {/* Time Display */}
        <div className="flex items-center justify-center text-sm font-mono text-muted-foreground">
          <span>{formatTime(replayState.current_time)}</span>
          <span className="mx-2">/</span>
          <span>{formatTime(replayState.max_time)}</span>
        </div>

        {/* Control Buttons and Speed Controls */}
        <div className="flex items-center justify-center gap-3">
          {/* Reset Button */}
          <button
            onClick={handleReset}
            className="bg-muted hover:bg-accent rounded p-1.5 transition-colors flex items-center justify-center"
            aria-label="Reset"
            title="Reset to beginning"
          >
            <RotateCcw size={14} className="text-foreground" strokeWidth={2} />
          </button>

          {/* Playback Controls */}
          <div className="flex items-center gap-2">
            <button
              onClick={handlePrevious}
              className="bg-muted hover:bg-accent rounded p-1.5 transition-colors flex items-center justify-center"
              aria-label="Previous"
              title="Back 30 seconds"
            >
              <SkipBack size={14} className="text-foreground" strokeWidth={2} />
            </button>

            <button
              onClick={handlePlayPause}
              className="bg-muted hover:bg-accent rounded p-1.5 transition-colors flex items-center justify-center"
              aria-label={replayState.is_playing ? "Pause" : "Play"}
            >
              {replayState.is_playing ? (
                <Pause size={14} className="text-foreground" strokeWidth={2} />
              ) : (
                <Play size={14} className="text-foreground ml-0.5" strokeWidth={2} />
              )}
            </button>

            <button
              onClick={handleNext}
              className="bg-muted hover:bg-accent rounded p-1.5 transition-colors flex items-center justify-center"
              aria-label="Next"
              title="Forward 30 seconds"
            >
              <SkipForward size={14} className="text-foreground" strokeWidth={2} />
            </button>
          </div>

          {/* Speed Control Button */}
          <button
            onClick={handleSpeedCycle}
            className="bg-blue-500 hover:bg-blue-600 text-white rounded px-3 py-1.5 text-xs font-medium transition-colors min-w-[45px]"
            aria-label={`${replayState.speed}x speed`}
          >
            {replayState.speed}x
          </button>
        </div>

        {/* Timeline Slider */}
        <div className="flex items-center justify-center px-1">
          <div className="w-[450px]">
            <Slider
              value={sliderValue}
              onValueChange={handleSliderChange}
              onValueCommit={handleSliderCommit}
              onPointerDown={() => setIsDragging(true)}
              max={100}
              step={0.1}
              className="w-full"
            />
          </div>
        </div>
      </div>
    </div>
  );
};
