"use client";
import type { ReactNode } from "react";
import { SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar";
import { AppSidebar } from "@/components/AppSidebar";
import { fetchAPList, fetchSTAMetrics, switchDataMode, DataMode } from "@/lib/api-dashboard";
import { useEffect, useState, useContext, useCallback } from "react";
import { ApCount, ClientCount, SimulationStateContext, DataModeContext } from "@/lib/contexts";
import CanvasPreview from "@/components/CanvasPreview";
import { ThemeSwitch } from "@/components/ThemeSwitch";

const TRANSITION_DURATION = 1000; // Match CanvasPreview transition duration

export default function Shell({ children }: { children: ReactNode }) {
  let [apList, setApList] = useState([]);
  let [clientCount, setClientCount] = useState(0);
  const [shouldShowContent, setShouldShowContent] = useState(false);
  const simulationContext = useContext(SimulationStateContext);

  // Data mode state - persisted across navigation
  const [dataMode, setDataModeState] = useState<DataMode>('replay');
  const [isMounted, setIsMounted] = useState(false);
  const [refreshKey, setRefreshKey] = useState(0);
  const [hasInitialized, setHasInitialized] = useState(false);


  // Initialize mode from localStorage on first mount only
  useEffect(() => {
    if (hasInitialized) return; // Don't re-run on subsequent mounts

    setIsMounted(true);
    setHasInitialized(true);

    const saved = localStorage.getItem('data-mode') as DataMode | null;
    const targetMode = saved === 'simulation' ? 'simulation' : 'replay';

    // Try to sync with backend
    switchDataMode(targetMode)
      .then(() => {
        setDataModeState(targetMode);
      })
      .catch(() => {
        // If simulation mode fails (no active sim), fall back to replay
        if (targetMode === 'simulation') {
          switchDataMode('replay').then(() => {
            setDataModeState('replay');
            localStorage.setItem('data-mode', 'replay');
          }).catch(console.error);
        }
      });
  }, [hasInitialized]);

  const setDataMode = useCallback(async (mode: DataMode) => {
    try {
      await switchDataMode(mode);
      setDataModeState(mode);
      setRefreshKey(prev => prev + 1); // Trigger refresh
      if (typeof window !== 'undefined') {
        localStorage.setItem('data-mode', mode);
      }
    } catch (error) {
      console.error('Failed to switch data mode:', error);
    }
  }, []);


  useEffect(() => {
    fetchAPList().then((v) => {
      setApList(v);
    })
  }, [refreshKey]) // Refetch when mode changes

  useEffect(() => {
    fetchSTAMetrics().then((metrics) => {
      setClientCount(metrics.aggregate_metrics.total_clients);
    }).catch(() => {
      // If fetch fails, keep count at 0
      setClientCount(0);
    })
  }, [refreshKey]) // Refetch when mode changes

  // Show dashboard content immediately - fade in during transition simultaneously
  useEffect(() => {
    if (typeof window === 'undefined') {
      setShouldShowContent(true);
      return;
    }

    const transitionFlag = localStorage.getItem('canvas-preview-transition');

    if (transitionFlag === 'true') {
      // Show content immediately so it fades in during the canvas preview transition
      // The dashboard will fade in (500ms) while canvas preview animates (1000ms)
      setShouldShowContent(true);
    } else {
      // No transition, show content immediately
      setShouldShowContent(true);
    }
  }, []);

  return (
    <ApCount.Provider value={apList.length}>
      <ClientCount.Provider value={clientCount}>
        <DataModeContext.Provider value={{ mode: dataMode, setMode: setDataMode, refreshKey }}>
          <SidebarProvider>
            <AppSidebar apList={apList} />
            <main className="flex-1  min-w-0 lg:w-50 px-6 py-6 lg:px-10">
              <SidebarTrigger />
              <div className={`mx-auto lg:w-[90%] w-[92%] transition-opacity ${shouldShowContent ? 'opacity-100 duration-500' : 'opacity-0 duration-0'}`}>
                {children}
              </div>
            </main>
            <CanvasPreview />
            <div className="fixed top-5 right-5 z-50">
              <ThemeSwitch />
            </div>
          </SidebarProvider>
        </DataModeContext.Provider>
      </ClientCount.Provider>
    </ApCount.Provider>
  );
}
