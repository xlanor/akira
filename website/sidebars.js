// @ts-check

/** @type {import('@docusaurus/plugin-content-docs').SidebarsConfig} */
const sidebars = {
  docs: [
    'intro',
    {
      type: 'category',
      label: 'Setup',
      collapsed: false,
      items: ['setup/registering', 'setup/connecting-your-first-host'],
    },
    'features',
    {
      type: 'category',
      label: 'Advanced',
      items: ['advanced/wireguard'],
    },
    {
      type: 'category',
      label: 'Reference',
      items: ['reference/io-bindings'],
    },
    'developing',
  ],
};

export default sidebars;
