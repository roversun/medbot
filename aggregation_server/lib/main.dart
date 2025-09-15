import 'dart:io';
import 'dart:async';
import 'dart:convert';

class AggregationServer {
  static final Map<String, Map<String, dynamic>> basicData = {};
  static final Map<String, DateTime> basicLastUpdate = {};
  static final Map<String, int> latencyData = {};
  static final Map<String, DateTime> lastUpdate = {};
  static final Map<String, Map<String, dynamic>> tracertData = {};
  static final Map<String, DateTime> tracertLastUpdate = {};
  static final Map<String, Map<String, dynamic>> netData = {};
  static final Map<String, DateTime> netDataLastUpdate = {};

  static final Map<String, Map<String, dynamic>> tracertRecords = {};
  static final Map<String, DateTime> tracertTimestamps = {};

  static final Map<String, Map<String, dynamic>> latency2Data = {};
  static final Map<String, DateTime> lastUpdate2 = {}; // 新增清理计时

  int port;

  AggregationServer(this.port);

  Future<void> start() async {
    final server = await HttpServer.bind(InternetAddress.anyIPv4, port);
    server.sessionTimeout = 15;
    print('Aggregation server started on port $port');

    Timer.periodic(const Duration(minutes: 5), (timer) {
      final now = DateTime.now();
      _cleanupOldEntries(
        tracertTimestamps,
        tracertRecords,
        now,
        const Duration(minutes: 5),
      );
    });

    Timer.periodic(const Duration(seconds: 5), (timer) {
      final now = DateTime.now();
      _cleanupOldEntries(
        lastUpdate2,
        latency2Data,
        now,
        const Duration(seconds: 5),
      );
    });

    // Timer for cleaning up old HTTP latency data (2 seconds)
    Timer.periodic(const Duration(seconds: 2), (timer) {
      final now = DateTime.now();
      _cleanupOldEntries(
        lastUpdate,
        latencyData,
        now,
        const Duration(seconds: 2),
      );
    });

    // Timer for cleaning up old tracert data (5 minutes)
    Timer.periodic(const Duration(seconds: 10), (timer) {
      final now = DateTime.now();
      _cleanupOldEntries(
        tracertLastUpdate,
        tracertData,
        now,
        const Duration(seconds: 300),
      );
    });

    // Timer for cleaning up old net data (3 seconds)
    Timer.periodic(const Duration(seconds: 3), (timer) {
      final now = DateTime.now();
      _cleanupOldEntries(
        netDataLastUpdate,
        netData,
        now,
        const Duration(seconds: 3),
      );
    });

    Timer.periodic(const Duration(seconds: 10), (timer) {
      final now = DateTime.now();
      _cleanupOldEntries(
        basicLastUpdate,
        basicData,
        now,
        const Duration(seconds: 30),
      );
    });

    await for (final request in server) {
      // Set CORS headers
      request.response.headers.add('Access-Control-Allow-Origin', '*');
      request.response.headers.add(
        'Access-Control-Allow-Methods',
        'GET, POST, PUT, DELETE',
      );
      request.response.headers.add(
        'Access-Control-Allow-Headers',
        'Content-Type',
      );
      request.response.headers.set('Connection', 'close');

      // Handle OPTIONS requests
      if (request.method == 'OPTIONS') {
        request.response.statusCode = HttpStatus.ok;
        request.response.close();
        continue;
      }

      try {
        print("method:${request.uri.path}");

        switch (request.uri.path) {
          case '/set_latency':
            if (request.method == 'POST') {
              final body = await utf8.decoder.bind(request).join();
              final data = json.decode(body) as Map<String, dynamic>;
              // print("set_latency:$data");

              final uniqueKey =
                  '${data['targetHost']}-${data['hospital']}-${data['endType']}';

              // print("uniqueKey:$uniqueKey");

              latency2Data[uniqueKey] = {
                'targetHost': data['targetHost'] as String,
                'endType': data['endType'] as String,
                'surgeon': data['surgeon'] as String,
                'hospital': data['hospital'] as String,
                'surgeryType': data['surgeryType'] as String,
                'distance': data['distance'] as String,
                'isRunning': data['isRunning'] as bool,
                'detection': {
                  'host': data['detection']['host'] as String,
                  'latency': double.parse(
                    data['detection']['latency'] as String,
                  ),
                  'jitter': double.parse(data['detection']['jitter'] as String),
                  'lossRate': double.parse(
                    data['detection']['lossRate'].toString().replaceAll(
                      '%',
                      '',
                    ),
                  ),
                  'speedMbps': double.parse(
                    data['detection']['speedMbps'] as String,
                  ),
                },
                'timestamp': DateTime.now().toIso8601String(),
              };
              lastUpdate2[uniqueKey] = DateTime.now();

              // print("latency2Data[uniqueKey]:${latency2Data[uniqueKey]}");

              request.response.statusCode = HttpStatus.ok;
              request.response.write('Latency2 data updated');
            } else {
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }

            break;

          case '/get_latency':
            if (request.method == 'GET') {
              final mergedRecords = <String, dynamic>{};
              final groupedByHost = <String, List<Map<String, dynamic>>>{};

              // 按targetHost分组
              for (var record in latency2Data.values) {
                groupedByHost
                    .putIfAbsent(record['targetHost'], () => [])
                    .add(record);
              }

              groupedByHost.forEach((host, records) {
                // 寻找需要合并的记录对
                for (var i = 0; i < records.length; i++) {
                  for (var j = i + 1; j < records.length; j++) {
                    final record1 = records[i];
                    final record2 = records[j];

                    if (record1['hospital'] != record2['hospital'] &&
                        record1['endType'] != record2['endType']) {
                      // 确定endType顺序
                      final type1Record =
                          record1['endType'] == '1' ? record1 : record2;
                      final type2Record =
                          record1['endType'] == '2' ? record1 : record2;

                      // 生成合并键
                      final mergedKey =
                          '${host}_${type1Record['hospital']}-${type2Record['hospital']}';

                      // 合并detection数据
                      final mergedDetection = {
                        'latency': (type1Record['detection']['latency'] +
                                type2Record['detection']['latency'])
                            .toStringAsFixed(2),
                        'jitter': ((type1Record['detection']['jitter'] +
                                    type2Record['detection']['jitter']) /
                                2)
                            .toStringAsFixed(2),
                        'lossRate':
                            ((type1Record['detection']['lossRate'] +
                                        type2Record['detection']['lossRate']) /
                                    2)
                                .toStringAsFixed(1) +
                            '%',
                        'speedMbps': (type1Record['detection']['speedMbps'] +
                                type2Record['detection']['speedMbps'])
                            .toStringAsFixed(2),
                      };

                      mergedRecords[mergedKey] = {
                        'targetHost': host,
                        'hospital':
                            '${type1Record['hospital']}-${type2Record['hospital']}',
                        'surgeon': type2Record['surgeon'],
                        'surgeryType': type2Record['surgeryType'],
                        'distance': type2Record['distance'],
                        'isRunning':
                            type1Record['isRunning'] &&
                            type2Record['isRunning'],
                        'detection': mergedDetection,
                        'timestamp': DateTime.now().toIso8601String(),
                      };
                    } else {
                      print("record1['hospital']=${record1['hospital']}");
                      print("record2['hospital']=${record2['hospital']}");
                      print("record1['endType']=${record1['endType']}");
                      print("record2['endType']=${record2['endType']}");
                    }
                  }
                }
              });

              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(mergedRecords));
            } else {
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/set_tracert':
            if (request.method == 'POST') {
              final body = await utf8.decoder.bind(request).join();
              final data = json.decode(body) as Map<String, dynamic>;

              // print("set_tracert:\n$data");

              final key =
                  '${data['targetHost']}-${data['hospital']}-${data['endType']}';

              tracertRecords[key] = {
                'targetHost': data['targetHost'],
                'endType': data['endType'],
                'surgeon': data['surgeon'],
                'hospital': data['hospital'],
                'surgeryType': data['surgeryType'],
                'distance': data['distance'],
                'isRunning': data['isRunning'],
                'linkData':
                    (data['linkData'] as List)
                        .map((e) => e as Map<String, dynamic>)
                        .toList(),
                'timestamp': DateTime.now().toIso8601String(),
              };
              tracertTimestamps[key] = DateTime.now();

              // print("tracertRecords[key]:${tracertRecords[key]}");
              request.response.statusCode = HttpStatus.ok;
              request.response.write('Tracert data stored');
            } else {
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/get_tracert':
            if (request.method == 'GET') {
              final mergedResults = <String, dynamic>{};
              final hostGroups = <String, List<Map<String, dynamic>>>{};

              // 按targetHost分组
              for (var record in tracertRecords.values) {
                if (record['isRunning'] == true) {
                  hostGroups
                      .putIfAbsent(record['targetHost'], () => [])
                      .add(record);
                }
              }

              hostGroups.forEach((host, records) {
                // 查找需要合并的记录对
                final type1Records =
                    records.where((r) => r['endType'] == '1').toList();
                final type2Records =
                    records.where((r) => r['endType'] == '2').toList();

                // print("type1Records:$type1Records,type2Records:$type2Records");

                for (final type1 in type1Records) {
                  for (final type2 in type2Records) {
                    if (type1['hospital'] != type2['hospital']) {
                      final mergedKey = '${host}_merged';

                      // 合并基础字段
                      final merged = {
                        'targetHost': host,
                        'surgeon': type1['surgeon'],
                        'hospital': '${type1['hospital']}-${type2['hospital']}',
                        'surgeryType': type2['surgeryType'],
                        'distance': type2['distance'],
                        'isRunning': type1['isRunning'] && type2['isRunning'],
                        'linkData': _mergeLinkData(
                          type1['linkData'] as List<Map<String, dynamic>>,
                          type2['linkData'] as List<Map<String, dynamic>>,
                        ),
                        'timestamp': DateTime.now().toIso8601String(),
                      };

                      mergedResults[mergedKey] = merged;
                    }
                  }
                }
              });

              // print("mergedResults:$mergedResults");

              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(mergedResults));
            } else {
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/set_basic':
            if (request.method == 'POST') {
              final body = await utf8.decoder.bind(request).join();
              final data = json.decode(body) as Map<String, dynamic>;
              final firstId = data['firstId'] as String;
              final secondId = data['secondId'] as String;
              final key = '$firstId-$secondId';

              basicData[key] = {
                'active': data['active'] as bool,
                'firstId': firstId,
                'secondId': secondId,
                'role': data['role'] as String,
                'procedure': data['procedure'] as String,
                'distance': data['distance'] as int,
                'timestamp': DateTime.now().toIso8601String(),
              };
              basicLastUpdate[key] = DateTime.now();

              request.response.statusCode = HttpStatus.ok;
              request.response.write('Basic data updated');
              // print("basicData: $basicData");
            } else {
              print("methodNotAllowed:${request.uri.path}");
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }

            break;
          case '/get_basic':
            if (request.method == 'GET') {
              final mergedData = <String, dynamic>{};
              final processedPairs = <String>{};

              for (final entry in basicData.entries) {
                final currentKey = entry.key;
                final parts = currentKey.split('-');
                final firstId = parts[0];
                final secondId1 = parts[1];
                final currentEntry = entry.value;

                if (processedPairs.contains(currentKey)) continue;

                // Look for pairs with same firstId and different secondId
                for (final otherKey in basicData.keys) {
                  if (otherKey == currentKey) continue;

                  final otherParts = otherKey.split('-');
                  if (otherParts[0] == firstId && otherParts[1] != secondId1) {
                    final secondId2 = otherParts[1];
                    final otherEntry = basicData[otherKey]!;

                    // Generate sorted key
                    // final sortedIds = [secondId1, secondId2]..sort();
                    final mergedKey1 = '$secondId1-$secondId2';
                    final mergedKey2 = '$secondId2-$secondId1';

                    if (processedPairs.contains(mergedKey1) ||
                        processedPairs.contains(mergedKey2)) {
                      continue;
                    }
                    processedPairs.addAll([currentKey, otherKey, mergedKey1]);

                    // Get procedure and distance from '本地端' role
                    String? procedure;
                    int? distance;
                    if (currentEntry['role'] == '本地端') {
                      procedure = currentEntry['procedure'] as String;
                      distance = currentEntry['distance'] as int;
                    } else if (otherEntry['role'] == '本地端') {
                      procedure = otherEntry['procedure'] as String;
                      distance = otherEntry['distance'] as int;
                    } else {
                      continue; // Skip if no '本地端' role
                    }

                    // Calculate latency
                    var totalLatency = latencyData[mergedKey1] ?? 0;
                    if (totalLatency == 0) {
                      totalLatency = latencyData[mergedKey2] ?? 0;
                    }
                    print(
                      "latencyData[mergedKey1]:${latencyData[mergedKey1]} ",
                    );
                    print(
                      "latencyData[mergedKey2]:${latencyData[mergedKey2]} ",
                    );
                    print("latency:$totalLatency");
                    final mergedKey =
                        currentEntry['role'] == '远程端' ? mergedKey1 : mergedKey2;
                    // print("currentEntry['role'] :${currentEntry['role']}");
                    // print("mergedKey:$mergedKey");

                    mergedData[mergedKey] = {
                      'firstId': firstId,
                      'secondId': mergedKey,
                      'active': currentEntry['active'] || otherEntry['active'],
                      'procedure': procedure,
                      'distance': distance,
                      'commdistance': distance * 1.3,
                      'latency': totalLatency,
                      'timestamp': DateTime.now().toIso8601String(),
                    };
                  }
                }

                // Add unpaired active entries
                if (!processedPairs.contains(currentKey) &&
                    currentEntry['active'] == true) {
                  mergedData[currentKey] = {
                    ...currentEntry,
                    'latency': latencyData[currentKey] ?? 0,
                  };
                  processedPairs.add(currentKey);
                }
              }

              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(mergedData));
              // print("get_basic merged data: $mergedData");
            } else {
              print("methodNotAllowed:${request.uri.path}");

              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;
          case '/set_latency_old':
            if (request.method == 'POST') {
              final body = await utf8.decoder.bind(request).join();
              final data = json.decode(body);
              final id = data['id'] as String;
              final lat = data['lat'] as int;
              latencyData[id] = lat;
              lastUpdate[id] = DateTime.now();
              request.response.statusCode = HttpStatus.ok;
              request.response.write('Latency data updated');
              print("body:$body");
            } else {
              print("methodNotAllowed:${request.uri.path}");

              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/get_latency_old':
            if (request.method == 'GET') {
              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(latencyData));
              // print("get_latency:$latencyData");
            } else {
              print("methodNotAllowed:${request.uri.path}");
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/set_tracert_old':
            if (request.method == 'POST') {
              final body = await utf8.decoder.bind(request).join();
              final data = json.decode(body) as Map<String, dynamic>;
              final firstId = data['firstId'] as String;
              final secondId = data['secondId'] as String;
              final key = '$firstId-$secondId';

              tracertData[key] = {
                'firstId': firstId,
                'secondId': secondId,
                'active': data['active'] as bool,
                'tracertResults': data['tracertResults'],
                'timestamp': DateTime.now().toIso8601String(),
              };
              tracertLastUpdate[key] = DateTime.now();

              request.response.statusCode = HttpStatus.ok;
              request.response.write('Tracert data updated');
              // print("tracert body:$body");
            } else {
              print("methodNotAllowed:${request.uri.path}");

              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/get_tracert_1':
            if (request.method == 'GET') {
              final mergedData = <String, dynamic>{};
              final processedKeys = <String>{};

              for (final entry in tracertData.entries) {
                final currentKey = entry.key;
                if (processedKeys.contains(currentKey)) continue;

                final parts = currentKey.split('-');
                final firstId = parts[0];
                final secondId1 = parts[1];
                final currentData = entry.value;

                // Find a pair with same firstId, different secondId, and active=true
                String? pairKey;
                for (final otherKey in tracertData.keys) {
                  if (otherKey == currentKey ||
                      processedKeys.contains(otherKey)) {
                    continue;
                  }

                  final otherParts = otherKey.split('-');
                  if (otherParts[0] == firstId &&
                      otherParts[1] != secondId1 &&
                      (currentData['active'] == true ||
                          tracertData[otherKey]!['active'] == true)) {
                    pairKey = otherKey;
                    break;
                  }
                }

                if (pairKey != null) {
                  final pairData = tracertData[pairKey]!;
                  final secondId2 = pairKey.split('-')[1];

                  // Get max hop from first record
                  final firstResults = List<dynamic>.from(
                    currentData['tracertResults'],
                  );
                  final maxHop =
                      firstResults.isEmpty
                          ? 0
                          : firstResults.last['hop'] as int;

                  // Process second record's results
                  List<dynamic> secondResults = List<dynamic>.from(
                    pairData['tracertResults'],
                  );

                  // Check if last hops match
                  print("Check if last hops match...");
                  bool lastHopMatches = false;
                  if (firstResults.isNotEmpty && secondResults.isNotEmpty) {
                    final lastFirst = firstResults.last;
                    final lastSecond = secondResults.last;
                    print("lastFirst:\n$lastFirst");
                    print("lastSecond:\n$lastSecond");
                    // Compare using IP address (modify field name as needed)
                    lastHopMatches = lastFirst['ip'] == lastSecond['ip'];
                  }
                  print("lastHopMatches=$lastHopMatches");
                  // Remove last hop from second results if match found
                  if (lastHopMatches && secondResults.isNotEmpty) {
                    print("secondResults.removeLast");
                    secondResults.removeLast();
                  }

                  // Reverse and reindex second results
                  secondResults = secondResults.reversed.toList();
                  int currentHop = maxHop + 1;
                  final adjustedResults =
                      secondResults.map<Map<String, dynamic>>((result) {
                        return {
                          ...result as Map<String, dynamic>,
                          'hop': currentHop++,
                        };
                      }).toList();

                  // Create merged entry
                  final mergedKey = '$firstId-$secondId1-$secondId2';
                  mergedData[mergedKey] = {
                    'firstId': firstId,
                    'secondIds': [secondId1, secondId2],
                    'active': currentData['active'] || pairData['active'],
                    "distance": currentData['distance'] + pairData['distance'],
                    "start_latitude": currentData['latitude'],
                    "end_latitude": pairData['latitude'],
                    "start_longitude": currentData['longitude'],
                    "end_longitude": pairData['longitude'],
                    'tracertResults': [...firstResults, ...adjustedResults],
                    'timestamp': DateTime.now().toIso8601String(),
                  };

                  processedKeys.addAll([currentKey, pairKey]);
                } else {
                  print("not found pair");
                  if (currentData['active'] == true) {
                    mergedData[currentKey] = currentData;
                  }
                  processedKeys.add(currentKey);
                }
              }

              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(mergedData));
              print("get_tracert merged:\n $mergedData");
            } else {
              print("methodNotAllowed:${request.uri.path}");
              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;
          case '/get_tracert_old':
            if (request.method == 'GET') {
              final mergedData = <String, dynamic>{};
              final processedPairs = <String>{};

              for (final entry in tracertData.entries) {
                final currentKey = entry.key;
                final parts = currentKey.split('-');
                final firstId = parts[0];
                final secondId1 = parts[1];
                final currentData = entry.value;

                // Find all pairs with same firstId and different secondId
                for (final otherKey in tracertData.keys) {
                  if (currentKey == otherKey) continue;

                  final otherParts = otherKey.split('-');
                  if (otherParts[0] == firstId && otherParts[1] != secondId1) {
                    final secondId2 = otherParts[1];
                    final pairData = tracertData[otherKey]!;

                    // Generate sorted key to avoid duplicates
                    final sortedIds = [secondId1, secondId2]..sort();
                    final mergedKey =
                        '$firstId-${sortedIds[0]}-${sortedIds[1]}';

                    // Skip if already processed
                    if (processedPairs.contains(mergedKey)) continue;
                    processedPairs.add(mergedKey);

                    // -- Merge Logic Start --
                    final firstResults = List<dynamic>.from(
                      currentData['tracertResults'],
                    );
                    final maxHop =
                        firstResults.isEmpty
                            ? 0
                            : firstResults.last['hop'] as int;

                    List<dynamic> secondResults = List<dynamic>.from(
                      pairData['tracertResults'],
                    );

                    // Remove overlapping last hop
                    if (firstResults.isNotEmpty && secondResults.isNotEmpty) {
                      final lastFirst = firstResults.last;
                      final lastSecond = secondResults.last;
                      if (lastFirst['ip'] == lastSecond['ip']) {
                        secondResults.removeLast();
                      }
                    }

                    // Reverse and reindex second results
                    secondResults = secondResults.reversed.toList();
                    int currentHop = maxHop + 1;
                    final adjustedResults =
                        secondResults.map<Map<String, dynamic>>((result) {
                          return {
                            ...result as Map<String, dynamic>,
                            'hop': currentHop++,
                          };
                        }).toList();

                    // Build merged entry
                    mergedData[mergedKey] = {
                      'firstId': firstId,
                      'secondIds': sortedIds,
                      'active':
                          currentData['active'] &&
                          pairData['active'], // AND relation
                      'distance':
                          currentData['distance'] + pairData['distance'],
                      'start_latitude': currentData['latitude'],
                      'end_latitude': pairData['latitude'],
                      'start_longitude': currentData['longitude'],
                      'end_longitude': pairData['longitude'],
                      'tracertResults': [...firstResults, ...adjustedResults],
                      'timestamp': DateTime.now().toIso8601String(),
                    };
                    // -- Merge Logic End --
                  }
                }

                // Add unpaired active entries
                if (!mergedData.containsKey(currentKey)) {
                  if (currentData['active'] == true) {
                    mergedData[currentKey] = currentData;
                  }
                }
              }

              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(mergedData));
              print("Merged tracert data: $mergedData");
            } else {
              print("methodNotAllowed:${request.uri.path}");

              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/set_netdata':
            if (request.method == 'POST') {
              final body = await utf8.decoder.bind(request).join();
              final data = json.decode(body) as Map<String, dynamic>;
              // print("netdata data:$data");
              // print("data['firstId'] =${data['firstId']}");
              // print("data['secondId']=${data['secondId']}");

              final firstId = data['firstId'] as String;
              final secondId = data['secondId'] as String;
              final key = '$firstId-$secondId';

              netData[key] = {
                'firstId': firstId,
                'secondId': secondId,
                'latency': data['latency'],
                'lossrate': data['lossrate'],
                'jitter': data['jitter'],
                'bitrate': data['bitrate'],
                'timestamp': DateTime.now().toIso8601String(),
              };
              netDataLastUpdate[key] = DateTime.now();

              request.response.statusCode = HttpStatus.ok;
              request.response.write('Network data updated');
              print("netdata body:$body");
            } else {
              print("methodNotAllowed:${request.uri.path}");

              request.response.statusCode = HttpStatus.methodNotAllowed;
            }
            break;

          case '/get_netdata':
            if (request.method == 'GET') {
              request.response.headers.contentType = ContentType.json;
              request.response.write(json.encode(netData));
              // print("get_netdata:$netData");
            } else {
              request.response.statusCode = HttpStatus.methodNotAllowed;
              print("methodNotAllowed:${request.uri.path}");
            }
            break;

          default:
            request.response.statusCode = HttpStatus.notFound;
        }
      } catch (e) {
        request.response.statusCode = HttpStatus.badRequest;
        request.response.write('Error processing request: $e');
        print('Error processing request: $e');
      } finally {
        await request.response.close(); // Ensure closure
      }
    }
  }

  void _cleanupOldEntries<K, V>(
    Map<K, DateTime> lastUpdateMap,
    Map<K, V> dataMap,
    DateTime now,
    Duration threshold,
  ) {
    final toRemove = <K>[];
    for (final entry in lastUpdateMap.entries) {
      if (now.difference(entry.value) > threshold) {
        toRemove.add(entry.key);
      }
    }
    for (final id in toRemove) {
      print("Data expired, removing record: $id");
      dataMap.remove(id);
      lastUpdateMap.remove(id);
    }
  }

  List<Map<String, dynamic>> _mergeLinkData(
    List<Map<String, dynamic>> type1Links,
    List<Map<String, dynamic>> type2Links,
  ) {
    final merged = List<Map<String, dynamic>>.from(type1Links);

    // 检查最后hop是否相同
    final lastHop1 = type1Links.isNotEmpty ? type1Links.last['ip'] : null;
    final lastHop2 = type2Links.isNotEmpty ? type2Links.last['ip'] : null;

    var adjustedType2 = List<Map<String, dynamic>>.from(type2Links);
    if (lastHop1 != null && lastHop2 != null && lastHop1 == lastHop2) {
      adjustedType2 = adjustedType2.sublist(0, adjustedType2.length - 1);
    }

    // 反转并重新编号
    final reversedLinks = adjustedType2.reversed.toList();
    final startHop = type1Links.isNotEmpty ? type1Links.last['hop'] + 1 : 1;

    for (var i = 0; i < reversedLinks.length; i++) {
      final link = Map<String, dynamic>.from(reversedLinks[i]);
      link['hop'] = startHop + i;
      merged.add(link);
    }

    return merged;
  }
}

void main(List<String> arguments) {
  const defaultPort = 9000;
  int port = defaultPort;

  if (arguments.isNotEmpty) {
    try {
      port = int.parse(arguments[0]);
    } catch (e) {
      print('Invalid port number provided. Using default port $defaultPort.');
    }
  }

  final server = AggregationServer(port);
  server.start();
}
