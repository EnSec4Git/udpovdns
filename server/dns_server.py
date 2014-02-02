"""
Created on 16.08.2010

@author: Jochen Ritzel

Modified by Yavor Papazov
"""

from twisted.internet import selectreactor
selectreactor.install()

from twisted.names import dns, server, client, cache
from twisted.application import service, internet
from twisted.internet import reactor, defer
from twisted.internet.protocol import DatagramProtocol
import random
import socket
import base64

FAKE_SUFFIX = "a.a"
OWN_NAME = "me.a"
SOCK_FAKE_SUFFIX = "sock." + FAKE_SUFFIX
SND_FAKE_SUFFIX = "snd." + FAKE_SUFFIX
RECV_FAKE_SUFFIX = "recv." + FAKE_SUFFIX
CLS_FAKE_SUFFIX = "cls." + FAKE_SUFFIX

PORT_NOT_OPEN = "PORTNOTOPEN"
PARTOK = "PARTOK"
LOCALHOST = "127.0.0.1"
MAX_TXT_RECORD_SIZE = 253

# TODO: Write at least a minimal amount of comments

class RecvFromUdp(DatagramProtocol):
    def __init__(self):
        self.receivedData = []

    def datagramReceived(self, data, (host, port)):
        print("Received data!: ", host, port, "\nData: ", data)
        self.receivedData.append((data, host, port))
    
    def getData(self):
        data = self.receivedData[0]
        del self.receivedData[0]
        return data

    def empty(self):
        return len(self.receivedData) == 0

class FakeResolver(client.Resolver):
    def __init__(self, servers):
        client.Resolver.__init__(self, servers=servers)
        self.ttl = 1
        self.ports = {}
        self.udpObjects = {}
        self.receivers = {}
        self.dataObjectsToSend = {}

    def _getSampleText(self, name):
        return base64.b32encode('Hi, there!')
        
    def _get_free_port(self):
        a = random.randint(1025, 65534)
        while True:
            if not a in self.ports:
                return a
            a = random.randint(1025, 65534)

    def _pad_base32(self, text):
        i = len(text)
        while i % 8 != 0:
            text = text + '='
            i = len(text)
        return text
    
    def _empty_txt_record_deferred(self, name):
        return defer.succeed([
        [dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(""),)], (), ()])
    
    def _localhost_a_record(self, name):
        return [[dns.RRHeader(name, dns.A, dns.IN, self.ttl, dns.Record_A('127.0.0.1', self.ttl),)], (), ()]
    
    def _chunks(self, l, n):
        """ Yield successive n-sized chunks from l.
        Attribution: Ned Batchdeler
        SO: http://stackoverflow.com/questions/312443/how-do-you-split-a-list-into-evenly-sized-chunks-in-python
        """
        for i in xrange(0, len(l), n):
            yield l[i:i+n]

    def lookupAddress(self, name, timeout = None):
        if name == OWN_NAME:
            return defer.succeed(self._localhost_a_record(name))
        if name.endswith(FAKE_SUFFIX):
            d = self.lookupText(name, timeout)
            def prepender(x):
                x[0].insert(0, self._localhost_a_record(name)[0][0])
                return x
            d.addCallback(prepender)
            return d
        return self._lookup(name, dns.IN, dns.A, timeout)

    def lookupNameserver(self, name, timeout = None):
        if name.endswith(FAKE_SUFFIX):
            return defer.succeed([[dns.RRHeader(name, dns.NS, dns.IN,
            self.ttl, dns.Record_NS(OWN_NAME, self.ttl),)], (), ()])
        return super(client.Resolver, self).lookupNameserver(self, name, timeout)

    def lookupText(self, name, timeout = None):
        print(name)
        if name.endswith(FAKE_SUFFIX):
            if name.endswith(SOCK_FAKE_SUFFIX):
                free_port = self._get_free_port()
                while True:
                    portSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    portSocket.setblocking(False)
                    try:
                        portSocket.bind(('127.0.0.1', free_port))
                    except:
                        pass
                    else:
                        self.ports[free_port] = portSocket
                        receiver = RecvFromUdp()
                        self.receivers[free_port] = receiver
                        self.udpObjects[free_port] = reactor.adoptDatagramPort(portSocket.fileno(), socket.AF_INET, receiver)
                        print("Gave port ", free_port)
                        break
                    free_port = self._get_free_port()
                return defer.succeed([
                [dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(str(free_port)),)], (), ()])
            elif name.endswith(SND_FAKE_SUFFIX):
                useful = name[:-len(SND_FAKE_SUFFIX)-1]
                parts = useful.split(".")
                local_port = int(parts[-1])
                print("Local port: ", local_port)
                if not local_port in self.ports:
                    print("Local port is closed...")
                    return defer.succeed([
                    [dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(PORT_NOT_OPEN),)],
                    (), ()])
                print("Local port is open!")
                remote_port = socket.ntohs(int(parts[-2]))
                remote_packed_addr_dec = socket.ntohl(int(parts[-3]))
                remote_packed_addr = ""
                for i in range(0, 4):
                    digit = remote_packed_addr_dec % 256
                    remote_packed_addr = chr(digit) + remote_packed_addr
                    remote_packed_addr_dec = remote_packed_addr_dec / 256
                remote_addr = socket.inet_ntop(socket.AF_INET, remote_packed_addr)
                conn_id = (remote_addr, remote_port, local_port)
                if not conn_id in self.dataObjectsToSend:
                    self.dataObjectsToSend[conn_id] = ""
                data = "".join(parts[0:-3])
                eot = False
                if data[-1] == '=':
                    eot = True
                    data = data[:-1]
                total_data = self.dataObjectsToSend[conn_id] + data
                self.dataObjectsToSend[conn_id] = total_data
                print(data)
                print(total_data)
                if eot: # End of transmission
                    padded_data = self._pad_base32(total_data)
                    my_socket = self.ports[local_port]
                    raw_data = base64.b32decode(padded_data)
                    print("Sending: ", raw_data)
                    print("Socket: ", my_socket)
                    print("TO: ", remote_addr, remote_port)
                    my_socket.sendto(raw_data, (remote_addr, remote_port))
                    del self.dataObjectsToSend[conn_id]
                return defer.succeed([
                [dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(PARTOK),)], (), ()])
            elif name.endswith(RECV_FAKE_SUFFIX):
                port_str = name[:-len(RECV_FAKE_SUFFIX)-1]
                port_int = int(port_str)
                print("Recv from:", port_int)
                if not port_int in self.udpObjects:
                    return self._empty_txt_record_deferred()
                datagramObject = self.receivers[port_int]
                if datagramObject.empty():
                    print("Recv is empty")
                    return self._empty_txt_record_deferred()
                data_not_encoded = datagramObject.getData()[0]
                data_encoded = base64.b32encode(data_not_encoded)
                data_encoded = data_encoded.translate(None, '=')
                print(data_not_encoded)
                print(data_encoded)
                chunk_list = list(self._chunks(data_encoded, MAX_TXT_RECORD_SIZE))
                print(chunk_list)
                responses = list(map(lambda x: dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(x),), chunk_list))
                print(responses)
                return defer.succeed([
                responses, (), ()])
            elif name.endswith(CLS_FAKE_SUFFIX):
                port_str = name[:-len(CLS_FAKE_SUFFIX)-1]
                port_int = int(port_str)
                if not port_int in self.ports:
                    return defer.succeed([
                    [dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(PORT_NOT_OPEN)),], (), ()])
                portSocket = self.ports[port_int]
                portSocket.close()
                del self.ports[port_int]
                del self.udpObjects[port_int]
                del self.receivers[port_int]
            return defer.succeed([
            [dns.RRHeader(name, dns.TXT, dns.IN, self.ttl, dns.Record_TXT(self._getSampleText(name)),),
            ], (), ()])
        else:
            return super(client.Resolver, self).lookupText(self, name, timeout)



## this sets up the application


application = service.Application('dnsserver', 1, 1)

# set up the fake resolver
fakeResolver = FakeResolver(servers = [('8.8.8.8', 53), ('8.8.4.4', 53)])

# create the protocols
f = server.DNSServerFactory(caches=[cache.CacheResolver()], clients=[fakeResolver])
p = dns.DNSDatagramProtocol(f)
f.noisy = p.noisy = False


# register as tcp and udp
ret = service.MultiService()
PORT=53

for (klass, arg) in [(internet.TCPServer, f), (internet.UDPServer, p)]:
    s = klass(PORT, arg)
    s.setServiceParent(ret)


# run all of the above as a twistd application
ret.setServiceParent(service.IServiceCollection(application))


# run it through twistd!
if __name__ == '__main__':
    import sys
    print "Usage: twistd -y %s" % sys.argv[0]
