# Changelog

## [1.9.0](https://github.com/victorstein/berean-os/compare/v1.8.0...v1.9.0) (2026-09-15)


### Features

* publication language setting, cancellable week scan, and one card walk ([#25](https://github.com/victorstein/berean-os/issues/25)) ([ded48d1](https://github.com/victorstein/berean-os/commit/ded48d173539b9eae75b494bf413707bcc79499f))

## [1.8.0](https://github.com/victorstein/berean-os/compare/v1.7.1...v1.8.0) (2026-09-14)


### Features

* a library for downloaded publications, and a Bible-centred sleep screen ([#18](https://github.com/victorstein/berean-os/issues/18)) ([28c4cdc](https://github.com/victorstein/berean-os/commit/28c4cdc311ffccb069f7d6e1b7d479f25825de45))

## [1.7.1](https://github.com/victorstein/berean-os/compare/v1.7.0...v1.7.1) (2026-09-14)


### Bug Fixes

* show this week's meeting publication and wait for a usable link ([#16](https://github.com/victorstein/berean-os/issues/16)) ([e87a68e](https://github.com/victorstein/berean-os/commit/e87a68e7b1ba7481fb0cf1e4e7275563584af263))

## [1.7.0](https://github.com/victorstein/berean-os/compare/v1.6.0...v1.7.0) (2026-09-14)


### Features

* launcher covers, catalog search, and the meetings library ([#14](https://github.com/victorstein/berean-os/issues/14)) ([b1412ef](https://github.com/victorstein/berean-os/commit/b1412ef7405db620da47f9abf208cde1176f652a))

## [1.6.0](https://github.com/victorstein/berean-os/compare/v1.5.0...v1.6.0) (2026-09-14)


### Features

* add the publication catalog index format ([3299c67](https://github.com/victorstein/berean-os/commit/3299c67af6254ea8cd00ac20e59f8ae9b55c5875))
* build and publish the publication catalog index in CI ([79d3e4f](https://github.com/victorstein/berean-os/commit/79d3e4fe6c35a1205b1a628015530face9f2e54e))

## [1.5.0](https://github.com/victorstein/berean-os/compare/v1.4.0...v1.5.0) (2026-09-14)


### Features

* long-press a word to start a passage selection ([7087e6d](https://github.com/victorstein/berean-os/commit/7087e6d68a09a08e3dffd2bc29f3bc7636d1f2d7))

## [1.4.0](https://github.com/victorstein/berean-os/compare/v1.3.1...v1.4.0) (2026-09-14)


### Features

* add the launcher home screen ([aab3ef1](https://github.com/victorstein/berean-os/commit/aab3ef12dfd2d75b6aae51cb7e93e844a6cf0058))
* drive the unassigned Back and Confirm indices from the nav keys ([0076e7b](https://github.com/victorstein/berean-os/commit/0076e7b168ed0e5f863defa2c5500139f41d7511))
* make the launcher the home screen ([ec65d86](https://github.com/victorstein/berean-os/commit/ec65d8628bed5b0c8897d49a39ead08c9bcfa086))
* show progress while the study migration runs ([5274585](https://github.com/victorstein/berean-os/commit/5274585ad3b52d16620630ab00126f2190bea4c3))
* synthesise Back and Confirm from the two nav keys ([706fded](https://github.com/victorstein/berean-os/commit/706fded52b6c9df529721c5f5099a42fe1fb58be))


### Bug Fixes

* resolve a held nav key on release, not mid-hold ([aab3ef1](https://github.com/victorstein/berean-os/commit/aab3ef12dfd2d75b6aae51cb7e93e844a6cf0058))


### Performance

* build the Bible book map in a handful of spine sweeps, not ~127 ([5274585](https://github.com/victorstein/berean-os/commit/5274585ad3b52d16620630ab00126f2190bea4c3))


### Documentation

* plan phase 2a, the input model ([9ee6d5d](https://github.com/victorstein/berean-os/commit/9ee6d5db0dc4520ed1aadf63f7eaf42dab609fb3))
* rewrite the phase 2a plan after review ([df56a37](https://github.com/victorstein/berean-os/commit/df56a371cfa46c0fe37acf093d793408574ed9f6))

## [1.3.1](https://github.com/victorstein/berean-os/compare/v1.3.0...v1.3.1) (2026-09-14)


### Bug Fixes

* resolve a tagged passage by address, not by its spine hint ([36d4e24](https://github.com/victorstein/berean-os/commit/36d4e246898fc934dee3d2676d25b638b8c6c7ad))

## [1.3.0](https://github.com/victorstein/berean-os/compare/v1.2.0...v1.3.0) (2026-09-14)


### Features

* add the global tag palette with retired-id tombstones ([d2a4e5c](https://github.com/victorstein/berean-os/commit/d2a4e5c746f37cd72a8b85aa8de58f802d776f8e))
* add the study store over atomic, streamed files ([f8147de](https://github.com/victorstein/berean-os/commit/f8147def92bdbb8c68f04f163da47edb89c6c26c))
* add the tagged passage record and its budgeted document ([cf1faa1](https://github.com/victorstein/berean-os/commit/cf1faa1111706e2e8f04f120de9bf426432f138f))
* add the Unit address type ([121e1e2](https://github.com/victorstein/berean-os/commit/121e1e2f79f4117ae180c5c071b54bdaee16246b))
* cache a publication's units lazily, one document at a time ([0b5fc91](https://github.com/victorstein/berean-os/commit/0b5fc91476a387a0648154560cc51ed1125d15ad))
* define the unit index on-disk format ([881fa0a](https://github.com/victorstein/berean-os/commit/881fa0ae64acb9a55e0db6601ea6b0a250394362))
* fingerprint a unit, and extract its visible text from one place ([4511379](https://github.com/victorstein/berean-os/commit/451137967fd8c210e805c606c5950fb7a99782f9))
* migrate the legacy highlight store, leaving it intact ([610b993](https://github.com/victorstein/berean-os/commit/610b993f343c918fb58bda9675ab7fa6854059bc))
* plan a legacy highlight's migration, and gate it on real data ([ca3f301](https://github.com/victorstein/berean-os/commit/ca3f30164008688ca57b32c1b9fbd289bc760a27))
* record a publication's symbol when the downloader knows it ([071259c](https://github.com/victorstein/berean-os/commit/071259ce3bdbe32fdbe0d773d5f1f7406befa773))
* recover a book path from a flattened store filename ([f89c61d](https://github.com/victorstein/berean-os/commit/f89c61d432fba07f146e40e168ca09bf3cea4efe))
* resolve a document offset to a unit, verse first ([d6b8cd7](https://github.com/victorstein/berean-os/commit/d6b8cd77a8f44577ced602ef2f23561d86ca749b))
* scan data-pid paragraph anchors ([78276af](https://github.com/victorstein/berean-os/commit/78276afc28f4ec9b4936928e939753bc389f7ca7))


### Bug Fixes

* run the study migration after the display is initialised ([7749daa](https://github.com/victorstein/berean-os/commit/7749daa4d1b84c4f6c4165f7ed8fb0da1c3deee1))


### Refactor

* point the reader activities at the study store ([0d586f6](https://github.com/victorstein/berean-os/commit/0d586f62a4d7062d4027d4bfe18af58bacf5fdb0))


### Documentation

* plan phase 1, the study store and tag migration ([b06296e](https://github.com/victorstein/berean-os/commit/b06296e3e5e9191b03b884754da0c0035bd5af8d))
* revise the phase 1 plan after adversarial review ([f5313f1](https://github.com/victorstein/berean-os/commit/f5313f1a624cfb7007b959fb68e7da5306b8f55d))

## [1.2.0](https://github.com/victorstein/berean-os/compare/v1.1.4...v1.2.0) (2026-09-14)


### Features

* give the boot and sleep screens their own identity ([455e9cd](https://github.com/victorstein/berean-os/commit/455e9cd1785636362f55a65ff955f0151bb3cdf4))

## [1.1.4](https://github.com/victorstein/berean-os/compare/v1.1.3...v1.1.4) (2026-09-14)


### Refactor

* stop identifying as CrossPoint to external services ([5e3a2f8](https://github.com/victorstein/berean-os/commit/5e3a2f89f158675c3f5782d6a6ff3c2fe0b2aa00))

## [1.1.3](https://github.com/victorstein/berean-os/compare/v1.1.2...v1.1.3) (2026-09-14)


### Bug Fixes

* remove the break statements the deleted cases left behind ([626cadb](https://github.com/victorstein/berean-os/commit/626cadbfbbe55d3317e2633845cc31b06d33ba0c))

## [1.1.2](https://github.com/victorstein/berean-os/compare/v1.1.1...v1.1.2) (2026-09-14)


### Refactor

* rename the device's network identity ([af0db1c](https://github.com/victorstein/berean-os/commit/af0db1cd523e81041574b994ef9a97aa8be1fc71))


### Documentation

* rewrite the project documents for bereanOS ([d009259](https://github.com/victorstein/berean-os/commit/d00925936a3b1e91c853a9f7b6d8ae843d398ca1))

## [1.1.1](https://github.com/victorstein/berean-os/compare/v1.1.0...v1.1.1) (2026-09-14)


### Documentation

* record the Phase 0 result ([31d9492](https://github.com/victorstein/berean-os/commit/31d9492b906f18a8d543a38b75dc80711e2d57da))

## [1.1.0](https://github.com/victorstein/berean-os/compare/v1.0.1...v1.1.0) (2026-09-14)


### Features

* add an atomic, budgeted save path for persisted stores ([97aa782](https://github.com/victorstein/berean-os/commit/97aa782a808130e91ab38eda1a4a31cd08f5e492))


### Refactor

* drop KOReader sync ([7380e02](https://github.com/victorstein/berean-os/commit/7380e026cdb56fe726613f74b0b19a2ba5fefe25))
* drop the dictionary ([d11a4c9](https://github.com/victorstein/berean-os/commit/d11a4c9f13b4f6eccf9f933ab8298c48043b7daa))
* drop the OPDS browser ([0d9285e](https://github.com/victorstein/berean-os/commit/0d9285ebe18a43e554b6f4c97aa4dbb98b53d668))
* drop the TXT and XTC readers ([a65c57d](https://github.com/victorstein/berean-os/commit/a65c57d80a7a75ded19ffd334eaff807b110d8d1))


### Documentation

* add five scoped agent definitions ([aa21618](https://github.com/victorstein/berean-os/commit/aa21618589779b15e7265a2828e4d52f7fcf8ae3))
* prune the agent guide to this device ([64c75b5](https://github.com/victorstein/berean-os/commit/64c75b589aeb00d0554fd8a9c6796b488dbed4c2))

## [1.0.1](https://github.com/victorstein/berean-os/compare/v1.0.0...v1.0.1) (2026-09-14)


### Refactor

* rename to bereanOS and point OTA at its own repo ([bd58e47](https://github.com/victorstein/berean-os/commit/bd58e47d5584a20722e31d25c848f6b18c02969b))

## 1.0.0 (2026-09-14)


### Documentation

* correct the workflow against the nicaraguan-laws original ([d57f1d3](https://github.com/victorstein/berean-os/commit/d57f1d37e3dc27945404137871f040ece8d3aac7))
* fold the repo-creation outcome back into the Phase 0 plan ([9bbad11](https://github.com/victorstein/berean-os/commit/9bbad11920b03c7b3f487a58c8196f8f6d30bc0a))
* found bereanOS with the reviewed design ([46bcf5e](https://github.com/victorstein/berean-os/commit/46bcf5e6e75717293ef57c06eda412ba33393085))
* plan Phase 0, the fork and strip ([6392556](https://github.com/victorstein/berean-os/commit/6392556bd47a5862b2f813502c7a9e1cc006f377))
* port the workflow and correct the inherited agent guide ([930fee1](https://github.com/victorstein/berean-os/commit/930fee1ac881764b5b4a01633c1441c6e096ad3d))
* rebuild the Phase 0 plan after adversarial review ([5ac9e26](https://github.com/victorstein/berean-os/commit/5ac9e26b12bed01689be83812a76bf463b9dbdd7))

## Changelog
