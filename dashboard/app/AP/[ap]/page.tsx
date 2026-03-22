"use client";

import StatisticCard12 from "@/components/ApStats"
import {ApChart, SystemInfo} from "@/components/SystemInfo"
import { ConnectedClientsTable } from "@/components/ConnectedClients";
import { ChannelInterferenceChart } from "@/components/ChannelInterferenceChart";
import { SimulationPanel } from "@/components/replay/SimulationPanel";
import { use, useEffect, useState, useContext } from "react";
import { fetchAPChannelUtilization, fetchSimulationUptime, type ChannelUtilizationDataPoint } from "@/lib/api-dashboard";
import { DataModeContext } from "@/lib/contexts";

type PageProps = {
  params: Promise<{ ap: string }>;
};

// Helper function to convert BSSID to a sequential number
function bssidToSequentialNumber(bssid: string): number {
    const cleanedBssid = bssid.replace(/:/g, ''); // Remove colons
    // Parse the last two characters as hexadecimal and convert to decimal
    const lastOctetHex = cleanedBssid.substring(cleanedBssid.length - 2);
    return parseInt(lastOctetHex, 16);
}

export default function AP({params}:PageProps){
  const { ap: apRaw } = use(params);
  const ap = decodeURIComponent(apRaw);
  const [channelUtilData, setChannelUtilData] = useState<ChannelUtilizationDataPoint[]>([]);
  const [timeResolution, setTimeResolution] = useState<string>("10");
  const [simulationUptime, setSimulationUptime] = useState<number | null>(null);
  const [apName, setApName] = useState<string>(ap); // Initialize with raw ap
  const dataModeContext = useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;
  const isSimulationMode = dataModeContext?.mode === 'simulation';
  const isReplayMode = dataModeContext?.mode === 'replay';

  // Determine AP name based on BSSID
  useEffect(() => {
    const sequentialNumber = bssidToSequentialNumber(ap);
    setApName(`Study Router ${sequentialNumber}`);
  }, [ap]);

  // Fetch simulation uptime when in simulation mode
  useEffect(() => {
    if (isSimulationMode) {
      fetchSimulationUptime().then((uptime) => {
        setSimulationUptime(uptime);
      });
    } else {
      setSimulationUptime(null);
    }
  }, [isSimulationMode, refreshKey]);

  // Fetch channel utilization data
  useEffect(() => {
    if (!ap) return;

    // In simulation mode, wait for uptime to be loaded before fetching
    if (isSimulationMode && simulationUptime === null) {
      setChannelUtilData([]);
      return;
    }

    let cancelled = false;
    let timer: number | undefined;

    const load = async () => {
      try {
        let series: ChannelUtilizationDataPoint[] = await fetchAPChannelUtilization(ap, parseInt(timeResolution));

        // In simulation mode, filter to show only up to simulation time
        if (isSimulationMode && simulationUptime !== null) {
          series = series.filter(point => point.sim_time_seconds <= simulationUptime);
        }

        if (!cancelled) setChannelUtilData(series);
      } catch (e: any) {
        console.error("Failed to fetch channel utilization:", e);
      }
    };

    load();
    // Refresh every 10 seconds
    // @ts-ignore
    timer = window.setInterval(load, 10_000);

    return () => {
      cancelled = true;
      if (timer) window.clearInterval(timer);
    };
  }, [ap, timeResolution, refreshKey, isSimulationMode, simulationUptime]);

  // Get latest channel utilization value
  const latestChannelUtil = channelUtilData.length > 0 && channelUtilData[channelUtilData.length - 1]?.channel_utilization != null
    ? channelUtilData[channelUtilData.length - 1].channel_utilization
    : null;

  return <div className="min-h-screen flex flex-col">
    <div className="flex-1 p-8">
    <div className="flex items-start justify-between mb-4">
      <div>
        <div className="font-inter font-medium text-[24px] leading-9 tracking-[0.07px]">{apName}</div>
        <div className="text-muted-foreground font-inter font-normal text-[16px] leading-6 tracking-[-0.31px]">Real-time monitoring and analytics</div>
      </div>
    </div>
    <div>
    <StatisticCard12 bssid={ap} refreshMs={10_000}/>
    <div className="grid gap-6 mt-6 grid-cols-1 lg:grid-cols-[minmax(0,1fr)_400px]">
            <div className="min-w-0">
              <ApChart
                bssid={ap}
                channelUtilData={channelUtilData}
                timeResolution={timeResolution}
                onTimeResolutionChange={setTimeResolution}
                simulationUptime={simulationUptime}
              />
            </div>
            <div className="min-w-0">
              <SystemInfo bssid={ap} refreshMs={2000} latestChannelUtilization={latestChannelUtil}/>
            </div>
    </div>
    <div className="mt-6">
    <ConnectedClientsTable bssid={ap} refreshMs={10_000} />
    </div>
    <div className="mt-6">
    <ChannelInterferenceChart bssid={ap}/>
    </div>
    </div>
    </div>

    {/* Simulation Panel - Only show in replay mode */}
    {isReplayMode && (
      <SimulationPanel />
    )}
    </div>
}