package mankool.mcBotClient.proxy;

import mankool.mcbot.protocol.Commands;
import meteordevelopment.meteorclient.systems.proxies.Proxies;
import meteordevelopment.meteorclient.systems.proxies.Proxy;
import meteordevelopment.meteorclient.systems.proxies.ProxyType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class MeteorProxyManager {
    private static final Logger LOGGER = LoggerFactory.getLogger(MeteorProxyManager.class);
    private static final String MANAGED_NAME = "mc-bot-manager";

    public static void apply(Commands.ProxyConfig config) {
        Proxies proxies = Proxies.get();

        // Remove any existing managed proxy by name
        Proxy existing = null;
        for (Proxy p : proxies) {
            if (MANAGED_NAME.equals(p.name.get())) {
                existing = p;
                break;
            }
        }
        if (existing != null) proxies.remove(existing);

        if (!config.getEnabled() || config.getHost().isEmpty()) {
            LOGGER.info("Proxy disabled");
            return;
        }

        ProxyType proxyType = config.getSocksVersion() == Commands.SocksVersion.SOCKS_VERSION_4
            ? ProxyType.Socks4 : ProxyType.Socks5;

        Proxy proxy = new Proxy.Builder()
            .name(MANAGED_NAME)
            .type(proxyType)
            .address(config.getHost())
            .port(config.getPort())
            .username(config.getUsername())
            .password(config.getPassword())
            .enabled(false)
            .build();

        proxies.add(proxy);
        proxies.setEnabled(proxy, true);

        LOGGER.info("Proxy configured ({}) {}:{}", proxyType, config.getHost(), config.getPort());
    }
}
