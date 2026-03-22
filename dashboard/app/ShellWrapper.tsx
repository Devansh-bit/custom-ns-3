"use client";
import { usePathname } from "next/navigation";
import Shell from "./shell";
import { SimulationStateContext, SimulationState } from "@/lib/contexts";
import { useState } from "react";

export default function ShellWrapper({ children }: { children: React.ReactNode }) {
  const pathname = usePathname();
  const isPlayground = pathname?.startsWith("/playground");
  const isReplay = pathname?.startsWith("/replay");
  const [simulationState, setSimulationState] = useState<SimulationState>("unsaved");

  if (isPlayground || isReplay) {
    return (
      <SimulationStateContext.Provider value={{ simulationState, setSimulationState }}>
        {children}
      </SimulationStateContext.Provider>
    );
  }

  return (
    <SimulationStateContext.Provider value={{ simulationState, setSimulationState }}>
      <Shell>{children}</Shell>
    </SimulationStateContext.Provider>
  );
}
