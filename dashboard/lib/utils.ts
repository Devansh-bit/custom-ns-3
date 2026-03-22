import { clsx, type ClassValue } from "clsx"
import { twMerge } from "tailwind-merge"
import { NetworkNode, NodeType } from "./types"

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}

export function readableMetricShown(str: string): string {
  const keymap = {
    latency: "Latency (ms)",
    jitter: "Jitter (ms)",
    packet_loss: "Packet Loss (%)",
    rssi: "RSSI (dBm)",
    snr: "SNR (dBm)",
  }

  return keymap[str]
}

export function nodeIdFromRelativeIndex(nodes: NetworkNode[], type: NodeType, idx: number): number | undefined {
  let type_count = 0;
  for (let cur = 0; cur < nodes.length; cur++) {
    const n = nodes[cur];
    if (n.type == type) {
      type_count++;
      if (type_count == idx) return n.id;
    }
  }
}

export function getRelativeIndex(nodes: NetworkNode[], type: NodeType, idx: number): number | undefined {
  let type_count = 0;
  for (let cur = 0; cur < nodes.length; cur++) {
    const n = nodes[cur];
    if (n.type == type) {
      type_count++;
      if (n.id == idx) return type_count;
    }
  }
}

