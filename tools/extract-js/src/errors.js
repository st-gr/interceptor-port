/** Thrown by `extract` for a file the extractor cannot use. The message names what was expected, in plain words for the player. */
export class ExtractError extends Error {
  constructor(message) {
    super(message);
    this.name = "ExtractError";
  }
}
