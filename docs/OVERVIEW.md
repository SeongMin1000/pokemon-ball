> ## Documentation Index
> Fetch the complete documentation index at: https://docs.edgeimpulse.com/llms.txt
> Use this file to discover all available pages before exploring further.

# Edge Impulse CLI

This Edge Impulse CLI is used to control local devices, act as a proxy to synchronise data for devices that don't have an internet connection, and to upload and convert local files. The CLI consists of several tools:

* [edge-impulse-daemon](/tools/clis/edge-impulse-cli/serial-daemon) - configures devices over serial, and acts as a proxy for devices that do not have an IP connection.
* [edge-impulse-uploader](/tools/clis/edge-impulse-cli/uploader) - allows uploading and signing local files.
* [edge-impulse-data-forwarder](/tools/clis/edge-impulse-cli/data-forwarder) - a very easy way to collect data from any supported device over a serial connection, and forward the data to Edge Impulse.
* [edge-impulse-run-impulse](/tools/clis/edge-impulse-cli/impulse-runner) - show the impulse running on your device.
* [edge-impulse-blocks](/tools/clis/edge-impulse-cli/blocks) - create organizational transformation, custom dsp, custom deployment and custom transfer learning blocks.
* [himax-flash-tool](/tools/clis/edge-impulse-cli/himax-flash-tool) - to flash the Himax boards and microcontrollers.

<Info>
  **Did you know you can also connect devices directly to your browser?**

  Recent versions of Google Chrome and Microsoft Edge can connect directly to fully-supported development boards, without the CLI. See [this blog post](https://edgeimpulse.com/blog/collect-sensor-data-straight-from-your-web-browser) for more information.
</Info>
