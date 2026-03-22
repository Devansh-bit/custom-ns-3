"""
Custom exceptions for Kafka Helper module.
"""


class KafkaHelperError(Exception):
    """Base exception for Kafka Helper errors."""
    pass


class ConnectionError(KafkaHelperError):
    """Raised when Kafka connection fails."""
    pass


class SerializationError(KafkaHelperError):
    """Raised when message serialization/deserialization fails."""
    pass


class ConfigurationError(KafkaHelperError):
    """Raised when configuration is invalid."""
    pass


class ProducerError(KafkaHelperError):
    """Raised when message production fails."""
    pass


class ConsumerError(KafkaHelperError):
    """Raised when message consumption fails."""
    pass
