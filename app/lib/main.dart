import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

void main() => runApp(const NicBleApp());

class NicBleApp extends StatelessWidget {
  const NicBleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: '4G NIC BLE',
      theme: ThemeData(colorScheme: ColorScheme.fromSeed(seedColor: Colors.teal), useMaterial3: true),
      home: const BleHomePage(),
    );
  }
}

class BleHomePage extends StatefulWidget {
  const BleHomePage({super.key});

  @override
  State<BleHomePage> createState() => _BleHomePageState();
}

class _BleHomePageState extends State<BleHomePage> {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _rx;
  BluetoothCharacteristic? _tx;

  final List<String> _logs = [];
  final TextEditingController _modeIdCtl = TextEditingController();
  StreamSubscription<List<ScanResult>>? _scanSub;

  static final Guid _svc = Guid('0000ff50-0000-1000-8000-00805f9b34fb');
  static final Guid _chrRx = Guid('0000ff51-0000-1000-8000-00805f9b34fb');
  static final Guid _chrTx = Guid('0000ff52-0000-1000-8000-00805f9b34fb');

  @override
  void dispose() {
    _scanSub?.cancel();
    _modeIdCtl.dispose();
    super.dispose();
  }

  void _log(String m) {
    setState(() => _logs.insert(0, '${DateTime.now().toIso8601String().substring(11, 19)} $m'));
    if (_logs.length > 80) {
      _logs.removeLast();
    }
  }

  Future<void> _scanAndConnect() async {
    if (!(await FlutterBluePlus.isSupported)) {
      _log('BLE not supported on this device');
      return;
    }
    final st = await FlutterBluePlus.adapterState.first;
    if (st != BluetoothAdapterState.on) {
      _log('Enable Bluetooth first');
      return;
    }

    _log('Scanning for 4G_NIC_CFG / 4G_NIC…');
    await _scanSub?.cancel();
    _scanSub = FlutterBluePlus.scanResults.listen((results) async {
      for (final r in results) {
        final n = r.device.platformName;
        if (n.contains('4G_NIC_CFG') || n.contains('4G_NIC')) {
          await FlutterBluePlus.stopScan();
          await _scanSub?.cancel();
          _scanSub = null;
          await _connect(r.device);
          return;
        }
      }
    });
    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
  }

  Future<void> _connect(BluetoothDevice d) async {
    try {
      _log('Connecting ${d.remoteId.str} …');
      await d.connect(timeout: const Duration(seconds: 15));
      _device = d;
      d.connectionState.listen((s) => _log('Link: $s'));

      final services = await d.discoverServices();
      _rx = null;
      _tx = null;
      for (final s in services) {
        if (s.uuid != _svc) {
          continue;
        }
        for (final c in s.characteristics) {
          if (c.uuid == _chrRx) {
            _rx = c;
          }
          if (c.uuid == _chrTx) {
            _tx = c;
          }
        }
      }

      if (_tx != null) {
        await _tx!.setNotifyValue(true);
        _tx!.onValueReceived.listen((v) {
          _log('← ${utf8.decode(v)}');
        });
      }

      setState(() {});
      _log(_rx != null && _tx != null ? 'GATT ready (FF50/51/52)' : 'Service 0xFF50 not found');
    } catch (e) {
      _log('Connect error: $e');
    }
  }

  Future<void> _disconnect() async {
    await _device?.disconnect();
    _device = null;
    _rx = null;
    _tx = null;
    setState(() {});
    _log('Disconnected');
  }

  Future<void> _send(String json) async {
    final c = _rx;
    if (c == null) {
      _log('Not connected');
      return;
    }
    try {
      await c.write(utf8.encode(json), withoutResponse: true);
      _log('→ $json');
    } catch (e) {
      _log('Write error: $e');
    }
  }

  void _setMode() {
    final id = int.tryParse(_modeIdCtl.text.trim());
    if (id == null || id < 0 || id > 255) {
      _log('Invalid work_mode_id');
      return;
    }
    _send('{"cmd":"set_mode","work_mode_id":$id}');
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('4G NIC BLE')),
      body: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                FilledButton(onPressed: _scanAndConnect, child: const Text('Scan & connect')),
                OutlinedButton(onPressed: _device != null ? _disconnect : null, child: const Text('Disconnect')),
                FilledButton.tonal(onPressed: () => _send('{"cmd":"ping"}'), child: const Text('Ping')),
                FilledButton.tonal(onPressed: () => _send('{"cmd":"get_mode"}'), child: const Text('Get mode')),
                FilledButton.tonal(onPressed: () => _send('{"cmd":"version"}'), child: const Text('Version')),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _modeIdCtl,
                    decoration: const InputDecoration(
                      labelText: 'work_mode_id',
                      border: OutlineInputBorder(),
                      isDense: true,
                    ),
                    keyboardType: TextInputType.number,
                    onSubmitted: (_) => _setMode(),
                  ),
                ),
                const SizedBox(width: 8),
                FilledButton(onPressed: _rx != null ? _setMode : null, child: const Text('Set mode')),
              ],
            ),
            const SizedBox(height: 12),
            Text('Log', style: Theme.of(context).textTheme.titleSmall),
            Expanded(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  border: Border.all(color: Theme.of(context).dividerColor),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: ListView.builder(
                  itemCount: _logs.length,
                  itemBuilder: (_, i) => Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                    child: SelectableText(_logs[i], style: const TextStyle(fontFamily: 'monospace', fontSize: 12)),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
