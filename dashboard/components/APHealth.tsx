"use client"

import { useEffect, useState, useContext } from "react"
import { Pie, PieChart } from "recharts"
import { fetchNetworkHealthUnified } from "@/lib/api-dashboard"
import { DataModeContext } from "@/lib/contexts"

import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import {
  ChartConfig,
  ChartContainer,
  ChartLegend,
} from "@/components/ui/chart"

// Use the same color palette as ClientPieChart for consistency
const chartColors = {
  good: "#3b82f6",   // bright blue
  fair: "#8b5cf6",   // purple
  poor: "#ec4899",   // pink
}

const chartConfig = {
  poor: {
    label: "Poor",
    color: chartColors.poor,
  },
  good: {
    label: "Good",
    color: chartColors.good,
  },
  fair: {
    label: "Fair",
    color: chartColors.fair,
  },
} satisfies ChartConfig

interface ChartEntry {
  payload: {
    review: string;
    percent: number;
    fill: string;
  };
}

type Props = {
  refreshMs?: number; // optional polling interval
};

export function APHealth({ refreshMs }: Props) {
  const [apScores, setApScores] = useState<number[]>([])
  const dataModeContext = useContext(DataModeContext);
  const refreshKey = dataModeContext?.refreshKey ?? 0;

  useEffect(() => {
    let cancelled = false;
    let timer: number | undefined;

    const getAPHealth = async () => {
      try {
        const data = await fetchNetworkHealthUnified()
        if (!cancelled) {
          // Extract scores from the dictionary
          const scores = Object.values(data.ap_health);
          setApScores(scores)
        }
      } catch (error) {
        if (!cancelled) {
          console.error("Failed to fetch AP health:", error)
        }
      }
    }

    getAPHealth();
    if (refreshMs && refreshMs > 0) {
      // @ts-ignore
      timer = window.setInterval(getAPHealth, refreshMs);
    }

    return () => {
      cancelled = true;
      if (timer) window.clearInterval(timer);
    };
  }, [refreshKey, refreshMs])

  const processedData = [
    {
      review: "Good",
      percent: apScores.filter((score) => score >= 90).length,
      fill: chartColors.good,
    },
    {
      review: "Fair",
      percent: apScores.filter((score) => score >= 50 && score < 90).length,
      fill: chartColors.fair,
    },
    {
      review: "Poor",
      percent: apScores.filter((score) => score < 50).length,
      fill: chartColors.poor,
    },
  ]

  return (
    <Card className="flex flex-col">
      <CardHeader>
        <CardTitle>AP Health</CardTitle>
      </CardHeader>
      <CardContent className="flex-1 flex flex-col">
        <ChartContainer config={chartConfig} className="flex flex-col flex-1">
          <PieChart>
            <Pie
              data={processedData}
              dataKey="percent"
              nameKey="review"
              labelLine={false}
              className="flex-1"
            />
            <ChartLegend
              verticalAlign="bottom"
              align="center"
              content={chartLegend}
              formatter={(value: string, entry: ChartEntry) => {
                const total = apScores.length
                const v = entry?.payload?.percent ?? 0
                const pct = total ? Math.round((v / total) * 100) : 0
                const name = entry?.payload?.review ?? value
                return `${name} ${pct}%`
              }}
            />
          </PieChart>
        </ChartContainer>
      </CardContent>
    </Card>
  )
}

function chartLegend(props) {
  const { payload } = props;

  return (
    <ul className="flex justify-between">
      {
        payload.map((entry, index) => (
          <li key={`item-${index}`} className={`flex flex-col items-center`}>
            <div className="h-4 w-4 rounded-sm mb-2" style={{ backgroundColor: entry.color }}></div>
            <div>{entry.payload.percent}</div>
            <div className="text-sm text-gray-400">{entry.value}</div>
          </li>
        ))
      }
    </ul>
  );
}
