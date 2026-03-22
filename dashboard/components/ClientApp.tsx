"use client";

import { useEffect, useState } from "react";
import { MetricCard } from "./MetricCard";
import { Video, Headphones, CreditCard } from "lucide-react";
import { fetchClientQuality } from "@/lib/api-dashboard";

export default function ClientApp() {
  const [clq, setClq] = useState(null);
  useEffect(() => {
    fetchClientQuality().then((v) => {
      v.video.icon = Video;
      v.video.title = "Video";
      v.video.subtitle = "Youtube, Google meet, Zoom, Instagram etc.";

      v.audio.icon = Headphones;
      v.audio.title = "Audio";
      v.audio.subtitle = "Spotify, Apple Music, Youtube Music etc.";

      v.transaction.icon = CreditCard;
      v.transaction.title = "Messaging";
      v.transaction.subtitle = "Instagram, Whatsapp, Slack, Reddit etc.";
      setClq(v);
    })
  }, [])

  return (
    <div className="mt-5 rounded-lg border bg-card p-6 shadow-sm">
      <div className="mx-auto max-w-7xl">
        <h1 className="mb-8">Clients Application Wise</h1>
        <div className="grid grid-cols-1 gap-6 lg:grid-cols-3">
          {clq ?
            <>
              <MetricCard icon={clq.video.icon} title={clq.video.title} subtitle={clq.video.subtitle} metrics={clq.video} />
              <MetricCard icon={clq.audio.icon} title={clq.audio.title} subtitle={clq.audio.subtitle} metrics={clq.audio} />
              <MetricCard icon={clq.transaction.icon} title={clq.transaction.title} subtitle={clq.transaction.subtitle} metrics={clq.transaction} />
            </>
            : null
          }
        </div>
      </div>
    </div>
  );
}
