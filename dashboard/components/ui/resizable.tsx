"use client"

import * as React from "react"
import * as ResizablePrimitive from "react-resizable-panels"

import { cn } from "@/lib/utils"

const ResizablePanelGroup = ({
  className,
  ...props
}: React.ComponentProps<typeof ResizablePrimitive.PanelGroup>) => (
  <ResizablePrimitive.PanelGroup
    className={cn(
      "flex h-full w-full data-[panel-group-direction=vertical]:flex-col",
      className
    )}
    {...props}
  />
)

const ResizablePanel = ResizablePrimitive.Panel

const ResizableHandle = ({
  withHandle,
  className,
  ...props
}: React.ComponentProps<typeof ResizablePrimitive.PanelResizeHandle> & {
  withHandle?: boolean
}) => (
  <ResizablePrimitive.PanelResizeHandle
    className={cn(
      "relative flex w-1 items-center justify-center bg-slate-200 hover:bg-slate-300 transition-colors after:absolute after:inset-y-0 after:left-1/2 after:w-1 after:-translate-x-1/2 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring focus-visible:ring-offset-1 data-[panel-group-direction=vertical]:h-px data-[panel-group-direction=vertical]:w-full data-[panel-group-direction=vertical]:after:left-0 data-[panel-group-direction=vertical]:after:h-1 data-[panel-group-direction=vertical]:after:w-full data-[panel-group-direction=vertical]:after:-translate-y-1/2 data-[panel-group-direction=vertical]:after:translate-x-0 [&[data-panel-group-direction=vertical]>div]:rotate-90",
      className
    )}
    {...props}
  >
    {withHandle && (
      <div className="z-10 flex h-8 w-4 items-center justify-center rounded-sm border-2 border-slate-300 bg-white shadow-sm hover:border-blue-400 hover:bg-blue-50 transition-colors">
        <svg
          xmlns="http://www.w3.org/2000/svg"
          width="16"
          height="16"
          viewBox="0 0 12 12"
          fill="none"
          className="text-slate-600"
        >
          <path
            d="M4 2C4 2.55228 4.44772 3 5 3H7C7.55228 3 8 2.55228 8 2C8 1.44772 7.55228 1 7 1H5C4.44772 1 4 1.44772 4 2Z"
            fill="currentColor"
          />
          <path
            d="M4 6C4 6.55228 4.44772 7 5 7H7C7.55228 7 8 6.55228 8 6C8 5.44772 7.55228 5 7 5H5C4.44772 5 4 5.44772 4 6Z"
            fill="currentColor"
          />
          <path
            d="M4 10C4 10.5523 4.44772 11 5 11H7C7.55228 11 8 10.5523 8 10C8 9.44772 7.55228 9 7 9H5C4.44772 9 4 9.44772 4 10Z"
            fill="currentColor"
          />
        </svg>
      </div>
    )}
  </ResizablePrimitive.PanelResizeHandle>
)

export { ResizablePanelGroup, ResizablePanel, ResizableHandle }

