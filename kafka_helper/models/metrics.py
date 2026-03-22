"""
Metrics models for ns3-metrics Kafka topic.

These models match the JSON format produced by kafka-producer.cc.
"""

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .common import Position, WifiBand


@dataclass
class ChannelNeighborInfo:
    """Information about a neighboring AP on a scanned channel."""
    bssid: str = ""
    rssi: float = 0.0
    channel: int = 0
    channel_width: int = 0
    sta_count: int = 0
    channel_utilization: float = 0.0
    wifi_utilization: float = 0.0
    non_wifi_utilization: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "bssid": self.bssid,
            "rssi": self.rssi,
            "channel": self.channel,
            "channel_width": self.channel_width,
            "sta_count": self.sta_count,
            "channel_utilization": self.channel_utilization,
            "wifi_utilization": self.wifi_utilization,
            "non_wifi_utilization": self.non_wifi_utilization,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ChannelNeighborInfo":
        """Create from dictionary."""
        return cls(
            bssid=str(data.get("bssid", "")),
            rssi=float(data.get("rssi", 0.0)),
            channel=int(data.get("channel", 0)),
            channel_width=int(data.get("channel_width", 0)),
            sta_count=int(data.get("sta_count", 0)),
            channel_utilization=float(data.get("channel_utilization", 0.0)),
            wifi_utilization=float(data.get("wifi_utilization", 0.0)),
            non_wifi_utilization=float(data.get("non_wifi_utilization", 0.0)),
        )


@dataclass
class ChannelScanData:
    """Scanning data for a specific channel."""
    channel_utilization: float = 0.0
    wifi_channel_utilization: float = 0.0
    non_wifi_channel_utilization: float = 0.0
    bssid_count: int = 0
    neighbors: List[ChannelNeighborInfo] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "channel_utilization": self.channel_utilization,
            "wifi_channel_utilization": self.wifi_channel_utilization,
            "non_wifi_channel_utilization": self.non_wifi_channel_utilization,
            "bssid_count": self.bssid_count,
            "neighbors": [n.to_dict() for n in self.neighbors],
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ChannelScanData":
        """Create from dictionary."""
        neighbors = [
            ChannelNeighborInfo.from_dict(n)
            for n in data.get("neighbors", [])
        ]
        return cls(
            channel_utilization=float(data.get("channel_utilization", 0.0)),
            wifi_channel_utilization=float(data.get("wifi_channel_utilization", 0.0)),
            non_wifi_channel_utilization=float(data.get("non_wifi_channel_utilization", 0.0)),
            bssid_count=int(data.get("bssid_count", 0)),
            neighbors=neighbors,
        )


@dataclass
class ConnectionMetrics:
    """Metrics for a single STA-AP connection."""
    sta_address: str = ""
    ap_address: str = ""
    node_id: int = 0
    sim_node_id: int = 0
    position: Position = field(default_factory=Position)
    mean_rtt_latency: float = 0.0
    jitter_ms: float = 0.0
    packet_count: int = 0
    uplink_throughput_mbps: float = 0.0
    downlink_throughput_mbps: float = 0.0
    packet_loss_rate: float = 0.0
    mac_retry_rate: float = 0.0
    ap_view_rssi: float = 0.0
    ap_view_snr: float = 0.0
    sta_view_rssi: float = 0.0
    sta_view_snr: float = 0.0
    last_update_seconds: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "sta_address": self.sta_address,
            "ap_address": self.ap_address,
            "node_id": self.node_id,
            "sim_node_id": self.sim_node_id,
            "position_x": self.position.x,
            "position_y": self.position.y,
            "position_z": self.position.z,
            "mean_rtt_latency": self.mean_rtt_latency,
            "jitter_ms": self.jitter_ms,
            "packet_count": self.packet_count,
            "uplink_throughput_mbps": self.uplink_throughput_mbps,
            "downlink_throughput_mbps": self.downlink_throughput_mbps,
            "packet_loss_rate": self.packet_loss_rate,
            "mac_retry_rate": self.mac_retry_rate,
            "ap_view_rssi": self.ap_view_rssi,
            "ap_view_snr": self.ap_view_snr,
            "sta_view_rssi": self.sta_view_rssi,
            "sta_view_snr": self.sta_view_snr,
            "last_update_seconds": self.last_update_seconds,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ConnectionMetrics":
        """Create from dictionary."""
        position = Position.from_flat(
            x=float(data.get("position_x", 0.0)),
            y=float(data.get("position_y", 0.0)),
            z=float(data.get("position_z", 0.0)),
        )
        return cls(
            sta_address=str(data.get("sta_address", "")),
            ap_address=str(data.get("ap_address", "")),
            node_id=int(data.get("node_id", 0)),
            sim_node_id=int(data.get("sim_node_id", 0)),
            position=position,
            mean_rtt_latency=float(data.get("mean_rtt_latency", 0.0)),
            jitter_ms=float(data.get("jitter_ms", 0.0)),
            packet_count=int(data.get("packet_count", 0)),
            uplink_throughput_mbps=float(data.get("uplink_throughput_mbps", 0.0)),
            downlink_throughput_mbps=float(data.get("downlink_throughput_mbps", 0.0)),
            packet_loss_rate=float(data.get("packet_loss_rate", 0.0)),
            mac_retry_rate=float(data.get("mac_retry_rate", 0.0)),
            ap_view_rssi=float(data.get("ap_view_rssi", 0.0)),
            ap_view_snr=float(data.get("ap_view_snr", 0.0)),
            sta_view_rssi=float(data.get("sta_view_rssi", 0.0)),
            sta_view_snr=float(data.get("sta_view_snr", 0.0)),
            last_update_seconds=float(data.get("last_update_seconds", 0.0)),
        )


@dataclass
class ApMetrics:
    """Metrics for a single Access Point."""
    node_id: int = 0
    sim_node_id: int = 0
    bssid: str = ""
    position: Position = field(default_factory=Position)
    channel: int = 0
    band: WifiBand = WifiBand.BAND_UNSPECIFIED
    channel_utilization: float = 0.0
    wifi_channel_utilization: float = 0.0
    non_wifi_channel_utilization: float = 0.0
    tx_power_dbm: float = 0.0
    obsspd_level_dbm: float = 0.0
    bss_color: int = 0
    associated_clients: int = 0
    client_list: List[str] = field(default_factory=list)
    bytes_sent: int = 0
    bytes_received: int = 0
    throughput_mbps: float = 0.0
    connection_metrics: Dict[str, ConnectionMetrics] = field(default_factory=dict)
    scanning_channel_data: Dict[int, ChannelScanData] = field(default_factory=dict)
    last_update_seconds: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "node_id": self.node_id,
            "sim_node_id": self.sim_node_id,
            "bssid": self.bssid,
            "position_x": self.position.x,
            "position_y": self.position.y,
            "position_z": self.position.z,
            "channel": self.channel,
            "band": self.band.value,
            "channel_utilization": self.channel_utilization,
            "wifi_channel_utilization": self.wifi_channel_utilization,
            "non_wifi_channel_utilization": self.non_wifi_channel_utilization,
            "tx_power_dbm": self.tx_power_dbm,
            "obsspd_level_dbm": self.obsspd_level_dbm,
            "bss_color": self.bss_color,
            "associated_clients": self.associated_clients,
            "client_list": self.client_list,
            "bytes_sent": self.bytes_sent,
            "bytes_received": self.bytes_received,
            "throughput_mbps": self.throughput_mbps,
            "connection_metrics": {
                k: v.to_dict() for k, v in self.connection_metrics.items()
            },
            "scanning_channel_data": {
                str(k): v.to_dict() for k, v in self.scanning_channel_data.items()
            },
            "last_update_seconds": self.last_update_seconds,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ApMetrics":
        """Create from dictionary."""
        position = Position.from_flat(
            x=float(data.get("position_x", 0.0)),
            y=float(data.get("position_y", 0.0)),
            z=float(data.get("position_z", 0.0)),
        )

        connection_metrics = {
            k: ConnectionMetrics.from_dict(v)
            for k, v in data.get("connection_metrics", {}).items()
        }

        scanning_channel_data = {
            int(k): ChannelScanData.from_dict(v)
            for k, v in data.get("scanning_channel_data", {}).items()
        }

        return cls(
            node_id=int(data.get("node_id", 0)),
            sim_node_id=int(data.get("sim_node_id", 0)),
            bssid=str(data.get("bssid", "")),
            position=position,
            channel=int(data.get("channel", 0)),
            band=WifiBand.from_string(data.get("band", "BAND_UNSPECIFIED")),
            channel_utilization=float(data.get("channel_utilization", 0.0)),
            wifi_channel_utilization=float(data.get("wifi_channel_utilization", 0.0)),
            non_wifi_channel_utilization=float(data.get("non_wifi_channel_utilization", 0.0)),
            tx_power_dbm=float(data.get("tx_power_dbm", 0.0)),
            obsspd_level_dbm=float(data.get("obsspd_level_dbm", 0.0)),
            bss_color=int(data.get("bss_color", 0)),
            associated_clients=int(data.get("associated_clients", 0)),
            client_list=list(data.get("client_list", [])),
            bytes_sent=int(data.get("bytes_sent", 0)),
            bytes_received=int(data.get("bytes_received", 0)),
            throughput_mbps=float(data.get("throughput_mbps", 0.0)),
            connection_metrics=connection_metrics,
            scanning_channel_data=scanning_channel_data,
            last_update_seconds=float(data.get("last_update_seconds", 0.0)),
        )


@dataclass
class MetricsMessage:
    """Complete metrics message from ns3-metrics topic."""
    timestamp_unix: int = 0
    sim_time_seconds: float = 0.0
    simulation_id: str = ""
    ap_metrics: Dict[str, ApMetrics] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "timestamp_unix": self.timestamp_unix,
            "sim_time_seconds": self.sim_time_seconds,
            "simulation_id": self.simulation_id,
            "ap_metrics": {
                k: v.to_dict() for k, v in self.ap_metrics.items()
            },
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any], simulation_id: str = "") -> "MetricsMessage":
        """Create from dictionary.

        Args:
            data: Parsed JSON dictionary
            simulation_id: Simulation ID (from Kafka key or explicit)
        """
        ap_metrics = {
            k: ApMetrics.from_dict(v)
            for k, v in data.get("ap_metrics", {}).items()
        }

        return cls(
            timestamp_unix=int(data.get("timestamp_unix", 0)),
            sim_time_seconds=float(data.get("sim_time_seconds", 0.0)),
            simulation_id=simulation_id or str(data.get("simulation_id", "")),
            ap_metrics=ap_metrics,
        )
