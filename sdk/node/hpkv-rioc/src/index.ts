export { RiocClient, RiocBatch, RiocBatchTracker, RangeQueryResult } from './client';
export { RiocConfig, RiocTlsConfig } from './config';
export {
  RiocError,
  RiocKeyNotFoundError,
  RiocInvalidParameterError,
  RiocMemoryError,
  RiocIOError,
  RiocProtocolError,
  RiocDeviceError,
  RiocBusyError
} from './errors';