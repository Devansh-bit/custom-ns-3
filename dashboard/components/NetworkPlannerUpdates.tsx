"use client";

import * as React from "react";
import { Card, CardContent } from "@/components/ui/card";
import {
    Collapsible,
    CollapsibleContent,
    CollapsibleTrigger,
} from "@/components/ui/collapsible";
import {
    Table,
    TableBody,
    TableCell,
    TableHead,
    TableHeader,
    TableRow,
} from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { ChevronDown, ChevronUp } from "lucide-react";
import type { NetworkPlannerUpdate } from "@/lib/api-dashboard";

type Props = {
    updates: NetworkPlannerUpdate[];
};

const INITIAL_COUNT = 5;
const LOAD_MORE_COUNT = 10;

// Generate unique key for each update
const getUniqueKey = (update: NetworkPlannerUpdate) =>
    `${update.type}-${update.planner_id}`;

// Check if fast loop update should be rejected
// Rejected if: network score dropped (negative delta) by more than 10%
// Accept if: delta >= 0 (score improved or stayed same)
const shouldRejectFastLoop = (update: NetworkPlannerUpdate): boolean => {
    if (update.type !== "fast" || update.delta_network_score == null) return false;

    // Accept any non-negative delta (score improved or stayed same)
    // Reject only if delta is negative (score dropped)
    // The delta represents the change in network score
    // Positive delta (+20, +5, etc.) = improvement = ACCEPT
    // Negative delta (-10, -5, etc.) = degradation = REJECT (rollback)
    return update.delta_network_score < 0;
};

export function NetworkPlannerUpdates({ updates }: Props) {
    const [openIds, setOpenIds] = React.useState<Set<string>>(new Set());
    const [selectedId, setSelectedId] = React.useState<string | null>(null);
    const [visibleCount, setVisibleCount] = React.useState(INITIAL_COUNT);
    const tileRefs = React.useRef<Map<string, HTMLDivElement>>(new Map());
    const tilesContainerRef = React.useRef<HTMLDivElement>(null);
    const timelineContainerRef = React.useRef<HTMLDivElement>(null);
    const [timelinePositions, setTimelinePositions] = React.useState<Map<string, number>>(new Map());

    // Filter updates to only show those with changes
    const updatesWithChanges = React.useMemo(() =>
        updates.filter(update => update.changes && update.changes.length > 0),
        [updates]
    );

    // Get visible updates based on pagination
    const visibleUpdates = React.useMemo(() =>
        updatesWithChanges.slice(0, visibleCount),
        [updatesWithChanges, visibleCount]
    );

    const hasMore = visibleCount < updatesWithChanges.length;

    const handleShowMore = () => {
        setVisibleCount(prev => Math.min(prev + LOAD_MORE_COUNT, updatesWithChanges.length));
    };


    const updateTimelinePositions = React.useCallback(() => {
        if (!tilesContainerRef.current || !timelineContainerRef.current) return;

        const positions = new Map<string, number>();
        const tilesContainerRect = tilesContainerRef.current.getBoundingClientRect();
        const timelineContainerRect = timelineContainerRef.current.getBoundingClientRect();

        tileRefs.current.forEach((tileEl, id) => {
            if (tileEl) {
                const tileRect = tileEl.getBoundingClientRect();
                const tileCenterYRelative = tileRect.top - tilesContainerRect.top + tileRect.height / 2;
                const timelineOffset = timelineContainerRect.top - tilesContainerRect.top;
                const timelineY = tileCenterYRelative - timelineOffset;
                positions.set(id, Math.max(0, timelineY));
            }
        });
        setTimelinePositions(positions);
    }, []);

    React.useEffect(() => {
        updateTimelinePositions();
        const handleResize = () => updateTimelinePositions();
        const handleScroll = () => updateTimelinePositions();

        window.addEventListener('resize', handleResize);
        window.addEventListener('scroll', handleScroll, true);

        return () => {
            window.removeEventListener('resize', handleResize);
            window.removeEventListener('scroll', handleScroll, true);
        };
    }, [updateTimelinePositions, openIds, visibleCount]);

    React.useEffect(() => {
        const timer = setTimeout(() => {
            updateTimelinePositions();
        }, 300);
        return () => clearTimeout(timer);
    }, [openIds, updateTimelinePositions, visibleCount]);

    const toggleUpdate = (uniqueKey: string) => {
        setOpenIds((prev) => {
            if (prev.has(uniqueKey)) {
                setSelectedId(null);
                return new Set();
            }
            setSelectedId(uniqueKey);
            return new Set([uniqueKey]);
        });
    };

    const formatTimestamp = (timestamp: number): string => {
        const date = new Date(timestamp);
        return date.toLocaleString("en-US", {
            month: "short",
            day: "numeric",
            year: "numeric",
            hour: "numeric",
            minute: "2-digit",
            hour12: true,
        });
    };

    const formatTimeShort = (timestamp: number): string => {
        const date = new Date(timestamp);
        return date.toLocaleString("en-US", {
            month: "short",
            day: "numeric",
            hour: "numeric",
            minute: "2-digit",
            hour12: true,
        });
    };

    // Format value with change indicator (old → new) or just value if unchanged
    const formatChangeValue = (
        oldValue: number | null | undefined,
        newValue: number | null | undefined,
        suffix: string = ""
    ): React.ReactNode => {
        // If new value is null but old value exists, show old value (unchanged)
        if (newValue == null && oldValue != null) {
            return `${oldValue}${suffix}`;
        }

        // If both are null, show "--"
        if (newValue == null) return "--";

        // If old value exists and is different, show change
        if (oldValue != null && oldValue !== newValue) {
            return (
                <span>
                    <span className="text-muted-foreground">{oldValue}{suffix}</span>
                    <span className="mx-1">→</span>
                    <span className="text-foreground font-medium">{newValue}{suffix}</span>
                </span>
            );
        }

        // Otherwise just show the value (unchanged or old value doesn't exist)
        return `${newValue}${suffix}`;
    };

    if (updatesWithChanges.length === 0) {
        return (
            <div className="flex items-center justify-center py-12 text-muted-foreground">
                No updates with changes available
            </div>
        );
    }

    return (
        <div className="grid grid-cols-1 lg:grid-cols-[180px_1fr] gap-8">
            {/* Timeline on the left */}
            <div className="hidden lg:block relative">
                <div className="sticky top-8">
                    <div
                        ref={timelineContainerRef}
                        className="relative ml-6 min-h-full"
                    >
                        {/* Vertical line */}
                        {visibleUpdates.length > 0 && (() => {
                            const positions = visibleUpdates
                                .map(update => timelinePositions.get(getUniqueKey(update)))
                                .filter((pos): pos is number => pos !== undefined);

                            if (positions.length > 0) {
                                const minPos = Math.min(...positions);
                                const maxPos = Math.max(...positions);
                                return (
                                    <div
                                        className="absolute left-3 w-0.5 bg-border transition-all duration-300"
                                        style={{
                                            top: `${minPos}px`,
                                            height: `${maxPos - minPos}px`,
                                        }}
                                    ></div>
                                );
                            }
                            return <div className="absolute left-3 top-0 bottom-0 w-0.5 bg-gray-300"></div>;
                        })()}

                        {/* Timeline items */}
                        {visibleUpdates.map((update, index) => {
                            const uniqueKey = getUniqueKey(update);
                            const isSelected = selectedId === uniqueKey;
                            const isOpen = openIds.has(uniqueKey);
                            const position = timelinePositions.get(uniqueKey);
                            const isRejected = update.type === "fast" && shouldRejectFastLoop(update);

                            if (position === undefined) return null;

                            return (
                                <div
                                    key={uniqueKey}
                                    className="absolute left-0 right-0 group transition-all duration-300"
                                    style={{
                                        top: `${position}px`,
                                        transform: 'translateY(-50%)',
                                        left: '2.05rem',
                                    }}
                                >
                                    <div className="absolute -left-8 top-1/2 -translate-y-1/2">
                                        <div
                                            className={`w-6 h-6 rounded-full border-4 transition-all ${
                                                isSelected
                                                    ? "bg-blue-500 border-blue-500 scale-125"
                                                    : isOpen
                                                    ? "bg-background border-blue-400"
                                                    : "bg-background border-border group-hover:border-blue-300"
                                            }`}
                                        />
                                    </div>

                                    <div className={`transition-all ${
                                        isSelected ? "text-blue-600 font-semibold" : "text-muted-foreground"
                                    }`}>
                                        <div className="text-xs text-muted-foreground mb-1">
                                            {formatTimeShort(update.time_of_update)}
                                        </div>
                                        <div className="text-sm font-medium">
                                            {update.type === "slow" ? "RL" : "Fast"} #{update.planner_id}
                                        </div>
                                        {/* Only show status badge for fast loop */}
                                        {update.type === "fast" && (
                                            <div className="text-xs mt-1">
                                                <Badge
                                                    variant={isRejected ? "destructive" : "success"}
                                                    size="sm"
                                                    appearance="default"
                                                    className="text-primary-foreground"
                                                >
                                                    {isRejected ? "Rejected" : "Accepted"}
                                                </Badge>
                                            </div>
                                        )}
                                    </div>
                                </div>
                            );
                        })}
                    </div>
                </div>
            </div>

            {/* Tiles on the right */}
            <div ref={tilesContainerRef} className="space-y-4 relative">
                {visibleUpdates.map((update) => {
                    const uniqueKey = getUniqueKey(update);
                    const isOpen = openIds.has(uniqueKey);
                    const isSlowLoop = update.type === "slow";
                    const isRejected = !isSlowLoop && shouldRejectFastLoop(update);

                    return (
                        <div
                            key={uniqueKey}
                            id={`update-${uniqueKey}`}
                            ref={(el) => {
                                if (el) {
                                    tileRefs.current.set(uniqueKey, el);
                                } else {
                                    tileRefs.current.delete(uniqueKey);
                                }
                            }}
                            className="scroll-mt-8"
                        >
                            <Collapsible
                                open={isOpen}
                                onOpenChange={() => toggleUpdate(uniqueKey)}
                            >
                                <Card className="overflow-hidden p-0 gap-0">
                                    <CollapsibleTrigger className="w-full">
                                        <CardContent className="cursor-pointer transition-colors py-6 flex items-center justify-between flex-row">
                                            <div>
                                                <div className="text-sm text-muted-foreground mb-1">
                                                    Update ID
                                                </div>
                                                <div className="font-semibold">
                                                    {update.planner_id}
                                                </div>
                                            </div>
                                            <div>
                                                <div className="text-sm text-muted-foreground mb-1">
                                                    Time of Update
                                                </div>
                                                <div>
                                                    {formatTimestamp(update.time_of_update)}
                                                </div>
                                            </div>
                                            <div>
                                                <div className="text-sm text-muted-foreground mb-1">
                                                    {isSlowLoop ? "Objective Function" : "Network Score"}
                                                </div>
                                                <div
                                                    className={`font-semibold ${
                                                        update.delta_network_score == null
                                                            ? "text-muted-foreground"
                                                            : update.delta_network_score >= 0
                                                                ? "text-green-600"
                                                                : "text-red-600"
                                                        }`}
                                                >
                                                    {update.delta_network_score != null
                                                        ? isSlowLoop
                                                            ? update.delta_network_score.toFixed(4)
                                                            : `${update.delta_network_score >= 0 ? "+" : ""}${update.delta_network_score.toFixed(2)}`
                                                        : "--"}
                                                </div>
                                            </div>

                                            <div className="flex items-center gap-4">
                                                <Badge
                                                    variant="outline"
                                                    size="md"
                                                    className={isSlowLoop ? "border-purple-500 text-purple-600" : "border-blue-500 text-blue-600"}
                                                >
                                                    {isSlowLoop ? "Slow Loop (RL)" : "Fast Loop"}
                                                </Badge>
                                                {/* Only show accept/reject for fast loop */}
                                                {!isSlowLoop && (
                                                    <Badge
                                                        variant={isRejected ? "destructive" : "success"}
                                                        size="md"
                                                        appearance="default"
                                                        className="text-primary-foreground"
                                                    >
                                                        {isRejected ? "Rejected" : "Accepted"}
                                                    </Badge>
                                                )}
                                                {isOpen ? (
                                                    <ChevronUp className="h-5 w-5 text-muted-foreground" />
                                                ) : (
                                                    <ChevronDown className="h-5 w-5 text-muted-foreground" />
                                                )}
                                            </div>
                                        </CardContent>
                                    </CollapsibleTrigger>

                                    <CollapsibleContent>
                                        <div className="border-t border-border p-6">
                                            <div className="mb-4">
                                                <h3 className="font-semibold mb-1">
                                                    AP {isSlowLoop ? "Proposed Changes" : "Configuration Changes"} ({update.changes.length})
                                                </h3>
                                                <p className="text-sm text-muted-foreground">
                                                    {isSlowLoop
                                                        ? "Proposed configuration changes from RL optimization"
                                                        : "Applied configuration changes"}
                                                </p>
                                            </div>

                                            {isSlowLoop ? (
                                                /* Slow Loop Table - Proposed values only, no OBSS PD */
                                                <Table className="text-xs">
                                                    <TableHeader>
                                                        <TableRow>
                                                            <TableHead className="font-normal text-muted-foreground">
                                                                MAC Address
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                Proposed Tx Power
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                Proposed Channel
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                Proposed Channel Width
                                                            </TableHead>
                                                        </TableRow>
                                                    </TableHeader>
                                                    <TableBody>
                                                        {update.changes.map((change, idx) => (
                                                            <TableRow key={`${uniqueKey}-change-${idx}`}>
                                                                <TableCell className="font-mono">
                                                                    {change.ap_id}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {change.updated_tx_power != null ? `${change.updated_tx_power} dBm` : "--"}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {change.updated_channel ?? "--"}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {change.updated_channel_width != null ? `${change.updated_channel_width} MHz` : "--"}
                                                                </TableCell>
                                                            </TableRow>
                                                        ))}
                                                    </TableBody>
                                                </Table>
                                            ) : (
                                                /* Fast Loop Table - Show old → new if changed */
                                                <Table className="text-xs">
                                                    <TableHeader>
                                                        <TableRow>
                                                            <TableHead className="font-normal text-muted-foreground">
                                                                MAC Address
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                Tx Power
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                Channel
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                Channel Width
                                                            </TableHead>
                                                            <TableHead className="font-normal text-center text-muted-foreground">
                                                                OBSS PD Threshold
                                                            </TableHead>
                                                        </TableRow>
                                                    </TableHeader>
                                                    <TableBody>
                                                        {update.changes.map((change, idx) => (
                                                            <TableRow key={`${uniqueKey}-change-${idx}`}>
                                                                <TableCell className="font-mono">
                                                                    {change.ap_id}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {formatChangeValue(
                                                                        change.old_tx_power,
                                                                        change.updated_tx_power,
                                                                        " dBm"
                                                                    )}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {formatChangeValue(
                                                                        change.old_channel,
                                                                        change.updated_channel
                                                                    )}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {formatChangeValue(
                                                                        change.old_channel_width,
                                                                        change.updated_channel_width,
                                                                        " MHz"
                                                                    )}
                                                                </TableCell>
                                                                <TableCell className="text-center tabular-nums">
                                                                    {formatChangeValue(
                                                                        change.old_obss_pd_threshold,
                                                                        change.updated_obss_pd_threshold,
                                                                        " dBm"
                                                                    )}
                                                                </TableCell>
                                                            </TableRow>
                                                        ))}
                                                    </TableBody>
                                                </Table>
                                            )}
                                        </div>
                                    </CollapsibleContent>
                                </Card>
                            </Collapsible>
                        </div>
                    );
                })}

                {/* Show More Button */}
                {hasMore && (
                    <div className="flex justify-center pt-4">
                        <Button
                            variant="outline"
                            onClick={handleShowMore}
                            className="w-full max-w-xs"
                        >
                            Show More ({updatesWithChanges.length - visibleCount} remaining)
                        </Button>
                    </div>
                )}

                {/* Show count indicator */}
                <div className="text-center text-sm text-muted-foreground pt-2">
                    Showing {Math.min(visibleCount, updatesWithChanges.length)} of {updatesWithChanges.length} updates with changes
                </div>
            </div>
        </div>
    );
}
