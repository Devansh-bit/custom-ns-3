#!/usr/bin/env python3
"""
CNN Kafka Consumer - Real-time CNN inference from Kafka spectrum stream

This replaces cnn_from_trace.py with a Kafka-based streaming approach.
Instead of reading trace files every 100ms, this consumes PSD data
directly from Kafka and runs CNN inference in real-time.

Data Flow:
    ns-3 SpectrumKafkaProducer ---> Kafka (spectrum-data topic)
            |
            v
    This Consumer (cnn_kafka_consumer.py)
            |
            v
    CNN Inference
            |
            v
    Kafka (cnn-predictions topic) ---> RL Agent / Other consumers

Usage:
    python cnn_kafka_consumer.py --brokers localhost:9092
"""

import os
import sys
import json
import time
import argparse
import logging
import numpy as np
from datetime import datetime
from typing import Dict, List, Optional, Any
from concurrent.futures import ThreadPoolExecutor
import threading

# Kafka
from confluent_kafka import Consumer, Producer, KafkaError, KafkaException

# Add path for CNN modules
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
CNN_PATH = os.path.join(PROJECT_ROOT, 'contrib', 'spectrum-pipe-streamer', 'examples')
sys.path.insert(0, CNN_PATH)

# Setup logging
LOG_DIR = os.path.join(PROJECT_ROOT, 'curr_cnn_logs')
os.makedirs(LOG_DIR, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(os.path.join(LOG_DIR, 'cnn_kafka_consumer.log')),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

# Try to import CNN modules
try:
    from CNN.image_one import create_in_memory_image
    from CNN.dutycycle_model import DeepSpectrum
    import torch
    CNN_AVAILABLE = True
    logger.info("CNN modules loaded successfully")
except ImportError as e:
    logger.warning(f"CNN modules not available: {e}")
    logger.warning("Running in pass-through mode (no inference)")
    CNN_AVAILABLE = False


class CNNKafkaConsumer:
    """
    Kafka consumer that receives spectrum PSD data and runs CNN inference.
    Publishes detection results to another Kafka topic.
    """
    
    def __init__(
        self,
        brokers: str = "localhost:9092",
        input_topic: str = "spectrum-data",
        output_topic: str = "cnn-predictions",
        group_id: str = "cnn-consumer-group",
        simulation_id: str = "sim-001"
    ):
        self.brokers = brokers
        self.input_topic = input_topic
        self.output_topic = output_topic
        self.group_id = group_id
        self.simulation_id = simulation_id
        
        self.consumer: Optional[Consumer] = None
        self.producer: Optional[Producer] = None
        self.running = False
        
        # CNN models
        self.model_2_4ghz = None
        self.model_5ghz = None
        
        # Processing stats
        self.windows_processed = 0
        self.inference_times = []
        
        # Thread pool for parallel inference
        self.executor = ThreadPoolExecutor(max_workers=4)
        
    def initialize(self) -> bool:
        """Initialize Kafka consumer/producer and load CNN models."""
        try:
            # Consumer config
            consumer_conf = {
                'bootstrap.servers': self.brokers,
                'group.id': self.group_id,
                'auto.offset.reset': 'latest',  # Only process new messages
                'enable.auto.commit': True,
                'session.timeout.ms': 30000,
            }
            self.consumer = Consumer(consumer_conf)
            self.consumer.subscribe([self.input_topic])
            logger.info(f"Kafka consumer initialized: {self.input_topic}")
            
            # Producer config for output
            producer_conf = {
                'bootstrap.servers': self.brokers,
                'linger.ms': 5,
                'batch.size': 16384,
            }
            self.producer = Producer(producer_conf)
            logger.info(f"Kafka producer initialized for: {self.output_topic}")
            
            # Load CNN models
            if CNN_AVAILABLE:
                self._load_models()
            
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize: {e}")
            return False
    
    def _load_models(self):
        """Load CNN models for 2.4GHz and 5GHz bands."""
        try:
            model_dir = os.path.join(CNN_PATH, 'CNN-model-weights')
            
            # 2.4 GHz model - DeepSpectrum takes weights path in constructor
            model_2_4_path = os.path.join(model_dir, 'bay2.pt')
            if os.path.exists(model_2_4_path):
                self.model_2_4ghz = DeepSpectrum(pretrained_weights_path=model_2_4_path)
                if self.model_2_4ghz.master_model:
                    self.model_2_4ghz.master_model.eval()
                    logger.info(f"Loaded 2.4GHz model: {model_2_4_path}")
                else:
                    logger.warning("2.4GHz model: master_model not available")
            
            # 5 GHz model
            model_5_path = os.path.join(model_dir, '5ghz.pt')
            if os.path.exists(model_5_path):
                self.model_5ghz = DeepSpectrum(pretrained_weights_path=model_5_path)
                if self.model_5ghz.master_model:
                    self.model_5ghz.master_model.eval()
                    logger.info(f"Loaded 5GHz model: {model_5_path}")
                else:
                    logger.warning("5GHz model: master_model not available")
                
        except Exception as e:
            logger.error(f"Failed to load CNN models: {e}")
    
    def run(self):
        """Main consumer loop - process messages until stopped."""
        if not self.consumer:
            logger.error("Consumer not initialized")
            return
        
        self.running = True
        logger.info("Starting CNN Kafka consumer loop...")
        
        try:
            while self.running:
                # Poll for message (timeout 100ms for responsiveness)
                msg = self.consumer.poll(0.1)
                
                if msg is None:
                    continue
                
                if msg.error():
                    if msg.error().code() == KafkaError._PARTITION_EOF:
                        continue
                    else:
                        logger.error(f"Consumer error: {msg.error()}")
                        continue
                
                # Process the message
                try:
                    self._process_message(msg)
                except Exception as e:
                    logger.error(f"Error processing message: {e}")
                    
        except KeyboardInterrupt:
            logger.info("Interrupted by user")
        finally:
            self.stop()
    
    def _process_message(self, msg):
        """Process a single Kafka message containing PSD data."""
        start_time = time.time()
        
        # Parse JSON payload
        try:
            payload = json.loads(msg.value().decode('utf-8'))
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON: {e}")
            return
        
        window_index = payload.get('window_index', 0)
        band = payload.get('band', '2.4GHz')
        psd_data = payload.get('psd_data', {})
        
        logger.info(f"Processing window {window_index} ({band})")
        
        # Run CNN inference
        detections = self._run_inference(psd_data, band)
        
        # Calculate inference time
        inference_time = time.time() - start_time
        self.inference_times.append(inference_time)
        self.windows_processed += 1
        
        # Build result
        result = {
            'window_index': window_index,
            'timestamp': datetime.now().isoformat(),
            'band': band,
            'simulation_id': payload.get('simulation_id', self.simulation_id),
            'inference_time_ms': inference_time * 1000,
            'detections': detections
        }
        
        # Publish to output topic
        self._publish_result(result, window_index)
        
        # Also save to local JSON file for debugging
        self._save_local_json(result, window_index)
        
        logger.info(f"Window {window_index}: {len(detections)} detections, "
                   f"inference={inference_time*1000:.1f}ms")
    
    def _run_inference(self, psd_data: Dict, band: str) -> List[Dict]:
        """Run CNN inference on PSD data."""
        detections = []
        
        if not CNN_AVAILABLE:
            # Return mock detections for testing
            import random
            if random.random() < 0.3:
                detections.append({
                    'technology': 'WiFi',
                    'confidence': round(random.uniform(0.6, 0.95), 4),
                    'frequency_range_mhz': [2412, 2437] if '2.4' in band else [5180, 5240]
                })
            if random.random() < 0.2:
                detections.append({
                    'technology': 'Bluetooth',
                    'confidence': round(random.uniform(0.5, 0.85), 4),
                    'frequency_range_mhz': [2402, 2480]
                })
            return detections
        
        # Select model based on band (DeepSpectrum wrapper with predict method)
        ds = self.model_2_4ghz if '2.4' in band else self.model_5ghz
        if ds is None or getattr(ds, 'master_model', None) is None:
            logger.warning(f"No model available for band {band}")
            return detections
        
        try:
            # Convert PSD data to spectrogram image
            for node_id, node_data in psd_data.items():
                frequencies = node_data.get('frequencies', [])
                psd_dbm = node_data.get('psd_avg_dbm', [])
                
                if not frequencies or not psd_dbm:
                    continue
                
                # Create spectrogram image
                psd_array = np.array(psd_dbm, dtype=np.float32)
                
                # Normalize to [0, 1] for CNN input
                # Typical range: -150 dBm to -50 dBm
                psd_normalized = (psd_array + 150) / 100.0
                psd_normalized = np.clip(psd_normalized, 0, 1)
                
                # Create 256x256 grayscale image as expected by DeepSpectrum
                # Resize PSD to width=256
                from scipy.ndimage import zoom
                target_width = 256
                zoom_factor = target_width / len(psd_normalized)
                psd_resized = zoom(psd_normalized, zoom_factor, order=1)
                
                # Create 256x256 image (stack PSD to create time dimension)
                spectrogram = np.tile(psd_resized, (256, 1))  # [256, 256]
                
                # Reshape for CNN: [batch, channels, height, width] = [1, 1, 256, 256]
                tensor = torch.from_numpy(spectrogram).unsqueeze(0).unsqueeze(0).float()
                
                # Run inference using ds.predict()
                with torch.no_grad():
                    preds = ds.predict(tensor)  # Returns [1, Nm, 4] tensor
                
                # Decode predictions
                preds_np = preds.cpu().numpy()
                if preds_np.ndim == 3 and preds_np.shape[0] == 1:
                    preds_np = preds_np[0]
                
                # Extract detections above confidence threshold
                tech_names = ['WiFi', 'Bluetooth', 'Zigbee', 'Microwave', 'Cordless'] \
                    if '2.4' in band else ['WiFi', 'Radar']
                
                for idx, p in enumerate(preds_np):
                    confidence = float(p[0])
                    if confidence > 0.3:  # Confidence threshold
                        # Determine technology from anchor index (simplified)
                        tech_idx = min(idx // 20, len(tech_names) - 1)
                        detections.append({
                            'technology': tech_names[tech_idx],
                            'confidence': round(confidence, 4),
                            'node_id': node_id,
                            'center_freq_norm': round(float(p[1]), 4) if len(p) > 1 else 0,
                            'bandwidth_norm': round(float(p[2]), 4) if len(p) > 2 else 0
                        })
                
        except Exception as e:
            logger.error(f"Inference error: {e}")
        
        return detections
    
    def _publish_result(self, result: Dict, window_index: int):
        """Publish CNN detection result to output Kafka topic."""
        if not self.producer:
            return
        
        try:
            key = f"{self.simulation_id}_{window_index}"
            value = json.dumps(result)
            
            self.producer.produce(
                self.output_topic,
                key=key.encode('utf-8'),
                value=value.encode('utf-8')
            )
            self.producer.poll(0)  # Trigger delivery callbacks
            
        except Exception as e:
            logger.error(f"Failed to publish result: {e}")
    
    def _save_local_json(self, result: Dict, window_index: int):
        """Save result to local JSON file for debugging."""
        try:
            filename = os.path.join(LOG_DIR, f'CNN_{window_index}.json')
            with open(filename, 'w') as f:
                json.dump(result, f, indent=2)
        except Exception as e:
            logger.error(f"Failed to save local JSON: {e}")
    
    def stop(self):
        """Stop the consumer and cleanup."""
        self.running = False
        
        if self.consumer:
            self.consumer.close()
            logger.info("Kafka consumer closed")
        
        if self.producer:
            self.producer.flush(timeout=5)
            logger.info("Kafka producer flushed")
        
        self.executor.shutdown(wait=False)
        
        # Print stats
        if self.inference_times:
            avg_time = sum(self.inference_times) / len(self.inference_times)
            logger.info(f"Processed {self.windows_processed} windows, "
                       f"avg inference time: {avg_time*1000:.1f}ms")


def main():
    parser = argparse.ArgumentParser(description='CNN Kafka Consumer')
    parser.add_argument('--brokers', default='localhost:9092',
                       help='Kafka broker addresses')
    parser.add_argument('--input-topic', default='spectrum-data',
                       help='Input topic for PSD data')
    parser.add_argument('--output-topic', default='cnn-predictions',
                       help='Output topic for CNN predictions')
    parser.add_argument('--group-id', default='cnn-consumer-group',
                       help='Kafka consumer group ID')
    parser.add_argument('--simulation-id', default='sim-001',
                       help='Simulation ID')
    
    args = parser.parse_args()
    
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║           CNN KAFKA CONSUMER - Real-time Inference           ║")
    print("╠══════════════════════════════════════════════════════════════╣")
    print(f"║  Brokers: {args.brokers:<49}║")
    print(f"║  Input:   {args.input_topic:<49}║")
    print(f"║  Output:  {args.output_topic:<49}║")
    print("╚══════════════════════════════════════════════════════════════╝")
    
    consumer = CNNKafkaConsumer(
        brokers=args.brokers,
        input_topic=args.input_topic,
        output_topic=args.output_topic,
        group_id=args.group_id,
        simulation_id=args.simulation_id
    )
    
    if not consumer.initialize():
        logger.error("Failed to initialize consumer")
        sys.exit(1)
    
    consumer.run()


if __name__ == '__main__':
    main()

