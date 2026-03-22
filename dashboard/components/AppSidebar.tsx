"use client";
import * as React from "react";
import { Home, Inbox, HelpCircle, LogOut, ChevronDown, Radio } from "lucide-react"
import Link from "next/link"
import { useEffect, useRef, useState } from "react"
import { usePathname } from "next/navigation"
import {
  Sidebar,
  SidebarContent,
  SidebarGroup,
  SidebarGroupContent,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarFooter,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from "@/components/ui/sidebar"
import { Separator } from "@/components/ui/separator";
import { Wifi } from "lucide-react";
import { DataModeToggle } from "@/components/DataModeToggle";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";

// Menu items.
const items = [
  {
    title: "Home",
    url: "/",
    icon: Home,
  },
  {
    title: "Client",
    url: "/clients",
    icon: Inbox,
  },
  {
    title: "RRM",
    url: "/rrm",
    icon: Radio,
  },
]

// Helper function to convert BSSID to a sequential number
function bssidToSequentialNumber(bssid: string): number {
    const cleanedBssid = bssid.replace(/:/g, ''); // Remove colons
    // Parse the last two characters as hexadecimal and convert to decimal
    const lastOctetHex = cleanedBssid.substring(cleanedBssid.length - 2);
    return parseInt(lastOctetHex, 16);
}


export function AppSidebar({ apList }) {
  const sidebarContentRef = React.useRef<HTMLDivElement>(null);
  const [isApOpen, setIsApOpen] = useState(true);
  const pathname = usePathname();

  useEffect(() => {
    const sidebarEl = sidebarContentRef.current;
    if (!sidebarEl) return;

    const handleWheel = (e: WheelEvent) => {
      e.preventDefault();
      sidebarEl.scrollTop += e.deltaY;
    };

    sidebarEl.addEventListener('wheel', handleWheel, { passive: false });
    
    return () => {
      sidebarEl.removeEventListener('wheel', handleWheel);
    };
  }, []);

  return (
    <Sidebar className="flex flex-col text-white">
      <SidebarHeader className="p-6 text-xl">
        <div className="flex items-center gap-4">
          <div className="rounded-xl bg-blue-500"><Wifi className="h-10 p-1 w-10 text-white" /></div>
          <div>
            <h1>WiFi Manager</h1>
            <span className="text-[#90A1B9] text-xs">Dashboard</span></div></div>
        <SidebarMenuButton asChild className="h-10 bg-blue-500 hover:bg-blue-500/90 hover:text-white">
          <a href="/playground" className="w-full text-center flex items-center justify-center">Playground</a>
        </SidebarMenuButton>
      </SidebarHeader>

      <SidebarContent 
        ref={sidebarContentRef}
        className="pl-6 pr-0.5"
        style={{ overscrollBehavior: 'contain' }}
      >
        {items.map((item) => {
          const isActive = pathname === item.url;
          return (
            <SidebarMenuButton asChild key={item.title} className={`h-10 ${isActive ? 'bg-slate-700/50' : ''}`}>
              <a href={item.url}>
                <item.icon />
                <span>{item.title}</span>
              </a>
            </SidebarMenuButton>
          );
        })}
        <Collapsible open={isApOpen} onOpenChange={setIsApOpen}>
          <CollapsibleTrigger className="w-full">
            <div className="h-10 flex items-center justify-between gap-2 pl-3 pr-1 cursor-pointer rounded-md w-full mb-2 hover:bg-slate-700/50 transition-colors">
              <div className="flex items-center gap-2">
              <Wifi className="text-slate-300" size={16} />
              <span className="text-slate-300 text-sm font-medium uppercase tracking-wide">AP<span className="normal-case">s</span></span>
              </div>
              <ChevronDown className={`text-slate-300 size-4 transition-transform duration-200 ${isApOpen ? 'rotate-180' : ''}`} />
            </div>
          </CollapsibleTrigger>
          <CollapsibleContent>
            <SidebarMenu>
              {apList
                .map((ap) => {
                  const isActive = pathname === `/AP/${ap.bssid}`;
                  const sequentialNumber = bssidToSequentialNumber(ap.bssid);
                  const apName = `Study Router ${sequentialNumber}`;
                  return (
                    <SidebarMenuButton key={ap.bssid} className={`text-slate-400 h-10 p-0 ${isActive ? 'bg-slate-700/50' : ''}`}>
                      <Link href={`/AP/${ap.bssid}`} className="h-full w-full pl-8 flex items-center">
                        {apName}
                      </Link>
                    </SidebarMenuButton>
                  );
              })}
            </SidebarMenu>
          </CollapsibleContent>
        </Collapsible>
      </SidebarContent>

      <SidebarFooter className="p-6">
        <DataModeToggle />
      </SidebarFooter>
    </Sidebar >
  )
}
