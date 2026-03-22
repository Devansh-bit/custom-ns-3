"""Bayesian Optimizer Package for WiFi Network Optimization

This package provides a modular Bayesian optimization system for WiFi network
parameter tuning using Optuna, Kafka messaging, and doubly-robust safety gates.

Main entry point: main.py
"""

from .core.optimizer import NetworkOptimizer
from .main import main

__all__ = ['NetworkOptimizer', 'main']
