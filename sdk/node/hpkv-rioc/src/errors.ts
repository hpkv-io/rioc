/**
 * Base error class for RIOC errors.
 */
export class RiocError extends Error {
  constructor(message: string, public code: number) {
    super(message);
    this.name = 'RiocError';
  }
}

/**
 * Error thrown when a key is not found.
 */
export class RiocKeyNotFoundError extends RiocError {
  constructor(key: string) {
    super(`Key not found: ${key}`, -6); // RIOC_ERR_NOENT
    this.name = 'RiocKeyNotFoundError';
  }
}

/**
 * Error thrown when invalid parameters are provided.
 */
export class RiocInvalidParameterError extends RiocError {
  constructor(message: string) {
    super(message, -1); // RIOC_ERR_PARAM
    this.name = 'RiocInvalidParameterError';
  }
}

/**
 * Error thrown when memory allocation fails.
 */
export class RiocMemoryError extends RiocError {
  constructor(message: string) {
    super(message, -2); // RIOC_ERR_MEM
    this.name = 'RiocMemoryError';
  }
}

/**
 * Error thrown when I/O operations fail.
 */
export class RiocIOError extends RiocError {
  constructor(message: string) {
    super(message, -3); // RIOC_ERR_IO
    this.name = 'RiocIOError';
  }
}

/**
 * Error thrown when protocol errors occur.
 */
export class RiocProtocolError extends RiocError {
  constructor(message: string) {
    super(message, -4); // RIOC_ERR_PROTO
    this.name = 'RiocProtocolError';
  }
}

/**
 * Error thrown when device errors occur.
 */
export class RiocDeviceError extends RiocError {
  constructor(message: string) {
    super(message, -5); // RIOC_ERR_DEVICE
    this.name = 'RiocDeviceError';
  }
}

/**
 * Error thrown when resource is busy.
 */
export class RiocBusyError extends RiocError {
  constructor(message: string) {
    super(message, -7); // RIOC_ERR_BUSY
    this.name = 'RiocBusyError';
  }
}

/**
 * Maps error codes to error classes.
 */
export function createError(code: number, message: string, key?: string): RiocError {
  switch (code) {
    case -1: // RIOC_ERR_PARAM
      return new RiocInvalidParameterError(message);
    case -2: // RIOC_ERR_MEM
      return new RiocMemoryError(message);
    case -3: // RIOC_ERR_IO
      return new RiocIOError(message);
    case -4: // RIOC_ERR_PROTO
      return new RiocProtocolError(message);
    case -5: // RIOC_ERR_DEVICE
      return new RiocDeviceError(message);
    case -6: // RIOC_ERR_NOENT
      return new RiocKeyNotFoundError(key || 'unknown');
    case -7: // RIOC_ERR_BUSY
      return new RiocBusyError(message);
    default:
      return new RiocError(message, code);
  }
}