"use client"

import * as React from "react"
import { useState, useContext } from "react"
import { TrendingUp } from "lucide-react"

import { Label, Pie, PieChart } from "recharts"
import { DataModeContext } from "@/lib/contexts"

import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"

import {
  ChartConfig,
  ChartContainer,
  ChartTooltip,
  ChartTooltipContent,
} from "@/components/ui/chart"

import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

import { ClientCount } from "@/lib/contexts"
import { fetchBandCounts, type BandCountRow, fetchMetricCounts, type MetricCountRow } from "@/lib/api-dashboard"

// Distinct color palette for better visibility
const chartColors = {
  color1: "#3b82f6", // bright blue
  color2: "#8b5cf6", // purple
  color3: "#ec4899", // pink
  color4: "#f59e0b", // amber
  color5: "#10b981", // emerald
}

// No static data - all data comes from API

const chartConfig = {
  visitors: {
    label: "Visitors",
  },
  Good: {
    label: "Good",
    color: chartColors.color1,
  },
  Fair: {
    label: "Fair",
    color: chartColors.color2,
  },
  Poor: {
    label: "Poor",
    color: chartColors.color3,
  },
  "2.4 GHz": {
    label: "2.4 GHz",
    color: chartColors.color1,
  },
  "5 GHz": {
    label: "5 GHz",
    color: chartColors.color2,
  },
  High: {
    label: "High",
    color: chartColors.color1,
  },
  Medium: {
    label: "Medium",
    color: chartColors.color2,
  },
  Low: {
    label: "Low",
    color: chartColors.color3,
  },
  Excellent: {
    label: "Excellent",
    color: chartColors.color1,
  },
  Windows: {
    label: "Windows",
    color: chartColors.color1,
  },
  macOS: {
    label: "macOS",
    color: chartColors.color2,
  },
  Linux: {
    label: "Linux",
    color: chartColors.color3,
  },
  iOS: {
    label: "iOS",
    color: chartColors.color4,
  },
  Android: {
    label: "Android",
    color: chartColors.color5,
  },
} satisfies ChartConfig

type GroupingOption = "health" | "band" | "dataSpeed" | "signalQuality" | "operatingSystem"

type Props = {
  refreshMs?: number; // optional polling interval
};

export default function ClientPieChart({ refreshMs }: Props) {
  const [selectedOption, setSelectedOption] = useState<GroupingOption>("dataSpeed")
  const totalClientCount = useContext(ClientCount)
  const dataModeContext = useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;
  const [bandData, setBandData] = React.useState<BandCountRow[]>([])
  const [loadingBand, setLoadingBand] = React.useState(false)
  const [bandDataLoaded, setBandDataLoaded] = React.useState(false)
  const [metricData, setMetricData] = React.useState<MetricCountRow[]>([])
  const [loadingMetric, setLoadingMetric] = React.useState(false)
  const [metricDataLoaded, setMetricDataLoaded] = React.useState(false)

  // Helper function to compare band data arrays
  const isBandDataEqual = (a: BandCountRow[], b: BandCountRow[]): boolean => {
    if (a.length !== b.length) return false;
    // Sort both arrays by band name for consistent comparison
    const sortedA = [...a].sort((x, y) => x.band.localeCompare(y.band));
    const sortedB = [...b].sort((x, y) => x.band.localeCompare(y.band));
    return sortedA.every((item, index) =>
      item.band === sortedB[index].band && item.sta_count === sortedB[index].sta_count
    );
  };

  // Helper function to compare metric data arrays
  const isMetricDataEqual = (a: MetricCountRow[], b: MetricCountRow[]): boolean => {
    if (a.length !== b.length) return false;
    // Sort both arrays by category for consistent comparison
    const sortedA = [...a].sort((x, y) => x.category.localeCompare(y.category));
    const sortedB = [...b].sort((x, y) => x.category.localeCompare(y.category));
    return sortedA.every((item, index) =>
      item.category === sortedB[index].category && item.sta_count === sortedB[index].sta_count
    );
  };

  // Fetch data when component mounts and when option is selected or mode changes
  React.useEffect(() => {
    let cancelled = false;
    let timer: number | undefined;

    const loadBandData = async () => {
      setLoadingBand(true)
      try {
        const data = await fetchBandCounts();
        if (!cancelled) {
          // Only update state if data has actually changed
          setBandData(prevData => {
            if (isBandDataEqual(prevData, data)) {
              return prevData; // Return same reference to prevent re-render
            }
            return data;
          });
          setBandDataLoaded(true)
        }
      } catch (error) {
        if (!cancelled) {
          console.error("Failed to fetch band counts:", error)
          setBandData([])
          setBandDataLoaded(true)
        }
      } finally {
        if (!cancelled) {
          setLoadingBand(false)
        }
      }
    };

    const loadMetricData = async (metricType: 'throughput' | 'rssi') => {
      setLoadingMetric(true)
      try {
        const data = await fetchMetricCounts(metricType);
        if (!cancelled) {
          // Only update state if data has actually changed
          setMetricData(prevData => {
            if (isMetricDataEqual(prevData, data)) {
              return prevData; // Return same reference to prevent re-render
            }
            return data;
          });
          setMetricDataLoaded(true)
        }
      } catch (error) {
        if (!cancelled) {
          console.error(`Failed to fetch ${metricType} counts:`, error)
          setMetricData([])
          setMetricDataLoaded(true)
        }
      } finally {
        if (!cancelled) {
          setLoadingMetric(false)
        }
      }
    };

    // Load data based on selected option
    if (selectedOption === "dataSpeed") {
      loadMetricData('throughput');
      if (refreshMs && refreshMs > 0) {
        // @ts-ignore
        timer = window.setInterval(() => loadMetricData('throughput'), refreshMs);
      }
    } else if (selectedOption === "signalQuality") {
      loadMetricData('rssi');
      if (refreshMs && refreshMs > 0) {
        // @ts-ignore
        timer = window.setInterval(() => loadMetricData('rssi'), refreshMs);
      }
    }

    return () => {
      cancelled = true;
      if (timer) window.clearInterval(timer);
    };
  }, [selectedOption, refreshKey, refreshMs])

  // Helper function to parse band label from API response
  const parseBandLabel = (band: string): string => {
    if (!band) return band;

    // Remove "BAND_" prefix if present (case insensitive)
    let cleaned = band.replace(/^BAND_/i, "")

    // Extract only discrete (integer) frequency numbers (e.g., "5", "6", "2", "4")
    // Match patterns like "5GHZ", "6GHZ", etc. - only whole numbers
    const frequencyMatch = cleaned.match(/(\d+)\s*GHZ?/i);
    if (frequencyMatch && frequencyMatch[1]) {
      return `${frequencyMatch[1]} GHz`;
    }

    // If no frequency pattern found, try to extract just integer numbers
    const numberMatch = cleaned.match(/(\d+)/);
    if (numberMatch && numberMatch[1]) {
      return `${numberMatch[1]} GHz`;
    }

    // Fallback: clean up and return
    cleaned = cleaned.replace(/_/g, " ").trim();
    return cleaned || band;
  }

  // Helper function to format category labels based on selection
  const formatCategoryLabel = (category: string, option: GroupingOption): string => {
    if (option === "dataSpeed") {
      switch (category) {
        case 'low':
          return 'Low (0-3 mbps)';
        case 'medium':
          return 'Medium (3-6 mbps)';
        case 'high':
          return 'High (6-10 mbps)';
        case 'very_high':
          return 'Very High (10+ mbps)';
        default:
          return category;
      }
    } else if (option === "signalQuality") {
      switch (category) {
        case 'good':
          return 'Good (> -50 dBm)';
        case 'fair':
          return 'Fair (-50 to -65 dBm)';
        case 'poor':
          return 'Poor (< -65 dBm)';
        default:
          return category;
      }
    }
    return category;
  }

  // Get chart data - only from API, no static data
  const chartData = React.useMemo(() => {
    // Get available distinct colors
    const availableColors = [chartColors.color1, chartColors.color2, chartColors.color3, chartColors.color4, chartColors.color5]

    // Show data for band option if API has loaded and we have data
    if (selectedOption === "band") {
      if (bandDataLoaded && bandData.length > 0) {
        return bandData.map((item, index) => {
          const bandLabel = parseBandLabel(item.band)
          // Cycle through available colors
          const fillColor = availableColors[index % availableColors.length]

          return {
            browser: bandLabel,
            visitors: item.sta_count,
            fill: fillColor,
          }
        })
      }
    }

    // Show data for metric-based options (dataSpeed, signalQuality) if API has loaded and we have data
    if (selectedOption === "dataSpeed" || selectedOption === "signalQuality") {
      if (metricDataLoaded && metricData.length > 0) {
        return metricData.map((item, index) => {
          const categoryLabel = formatCategoryLabel(item.category, selectedOption)
          // Cycle through available colors
          const fillColor = availableColors[index % availableColors.length]

          return {
            browser: categoryLabel,
            visitors: item.sta_count,
            fill: fillColor,
          }
        })
      }
    }

    // For other options, return empty array (no API endpoints yet)
    return []
  }, [bandData, bandDataLoaded, metricData, metricDataLoaded, selectedOption])

  // Calculate total from API data only
  const totalVisitors = React.useMemo(() => {
    return chartData.reduce((acc, curr) => acc + curr.visitors, 0)
  }, [chartData])

  // Use API data as-is, no scaling
  const scaledChartData = React.useMemo(() => {
    return chartData
  }, [chartData])

  return (
    <div className="w-full h-full flex items-center justify-center">
      <Card className="flex flex-col w-full h-full !shadow-md pb-2 overflow-hidden !rounded-2xl">
        <CardHeader className="pb-0 pt-4 relative flex-shrink-0">
          <div className="flex items-start sm:items-center justify-between mb-0 gap-2 flex-wrap">
            <div className="min-w-0 flex-1">
              <CardTitle className="mb-0.5">Clients</CardTitle>
              <CardDescription className="mt-0 mb-0">Grouped by {selectedOption === "dataSpeed" ? "Data Speed" : selectedOption === "signalQuality" ? "Signal Quality" : selectedOption === "operatingSystem" ? "Operating System" : selectedOption === "band" ? "Band" : "Health"}</CardDescription>
            </div>
            <div className="relative flex-shrink-0 -ml-2">
              <Select value={selectedOption} onValueChange={(value) => setSelectedOption(value as GroupingOption)}>
                <SelectTrigger
                  className="h-7 w-[140px] sm:w-[160px] rounded-lg pl-2.5"
                  aria-label="Select grouping"
                >
                  <SelectValue placeholder="Select grouping" />
                </SelectTrigger>
                <SelectContent
                  align="end"
                  side="bottom"
                  sideOffset={4}
                  alignOffset={-8}
                  className="rounded-xl w-[var(--radix-select-trigger-width)] max-w-[calc(100vw-1rem)]"
                  position="popper"
                >
                  <SelectItem value="dataSpeed" className="rounded-lg">
                    Data Speed
                  </SelectItem>
                  <SelectItem value="health" className="rounded-lg">
                    Health
                  </SelectItem>
                  <SelectItem value="signalQuality" className="rounded-lg">
                    Signal Quality
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>
          </div>
        </CardHeader>
        <CardContent className="flex-1 pb-0 pt-0 min-h-0 overflow-hidden">
          {chartData.length === 0 ? (
            <div className="flex items-center justify-center h-full">
              <p className="text-gray-500 text-sm">
                {(loadingBand || loadingMetric) ? "Loading..." : "No data available"}
              </p>
            </div>
          ) : (
            <ChartContainer
              config={chartConfig}
              className="mx-auto aspect-square max-h-[200px] h-full"
            >
              <PieChart>
                <ChartTooltip
                  cursor={false}
                  content={<ChartTooltipContent hideLabel />}
                />
                <Pie
                  data={scaledChartData}
                  dataKey="visitors"
                  nameKey="browser"
                  innerRadius={60}
                  strokeWidth={5}
                >
                  <Label
                    content={({ viewBox }) => {
                      if (viewBox && "cx" in viewBox && "cy" in viewBox) {
                        return (
                          <text
                            x={viewBox.cx}
                            y={viewBox.cy}
                            textAnchor="middle"
                            dominantBaseline="middle"
                          >
                            <tspan
                              x={viewBox.cx}
                              y={viewBox.cy}
                              className="fill-foreground text-3xl font-bold"
                            >
                              {totalVisitors.toLocaleString()}
                            </tspan>
                            <tspan
                              x={viewBox.cx}
                              y={(viewBox.cy || 0) + 24}
                              className="fill-muted-foreground"
                            >
                              Clients
                            </tspan>
                          </text>
                        )
                      }
                    }}
                  />
                </Pie>
              </PieChart>
            </ChartContainer>
          )}
        </CardContent>
      </Card>
    </div>
  )
}

