"use client";

import { createContext, Dispatch, SetStateAction } from "react";
import { DataMode } from "./api-dashboard";

export const ApCount = createContext(0);
export const ClientCount = createContext(0);

export const DataModeContext = createContext<{
  mode: DataMode;
  setMode: (mode: DataMode) => void;
  refreshKey: number; // Increments when mode changes to trigger data refetch
} | null>(null);

export type SimulationState =
  | "unsaved"
  | "building"
  | "built"
  | "starting"
  | "running"
  | "stopping";

export const SimulationStateContext = createContext<{
  simulationState: SimulationState;
  setSimulationState: Dispatch<SetStateAction<SimulationState>>;
} | null>(null);

export const SelectedClientContext = createContext<{
  selectedClient: string | null;
  setSelectedClient: Dispatch<SetStateAction<string | null>>;
} | null>(null);

